/*-------------------------------------------------------------------------------------
Copyright (c) 2006 John Judnich

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:
	1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
	2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
	3. This notice may not be removed or altered from any source distribution.
-------------------------------------------------------------------------------------*/

//BatchPage.cpp
//BatchPage is an extension to PagedGeometry which displays entities as static geometry.
//-------------------------------------------------------------------------------------

#include "BatchPage.h"
#include "BatchedGeometry.h"
#include "ShaderHelper.h"

#include <OgreRoot.h>
#include <OgreCamera.h>
#include <OgreVector.h>
#include <OgreQuaternion.h>
#include <OgreEntity.h>
#include <OgreRenderSystem.h>
#include <OgreRenderSystemCapabilities.h>
#include <OgreHighLevelGpuProgram.h>
#include <OgreHighLevelGpuProgramManager.h>
#include <OgreLogManager.h>
#include <OgreTechnique.h>

using namespace Ogre;

namespace Forests {

//-------------------------------------------------------------------------------------


unsigned long BatchPage::refCount = 0;
unsigned long BatchPage::GUID = 0;


void BatchPage::init(PagedGeometry *_geom, const Any &data)
{
	geom = _geom;
	int datacast = !data.has_value() ? 0 : Ogre::any_cast<int>(data);
#ifdef _DEBUG
	if ( datacast < 0)
		OGRE_EXCEPT(Exception::ERR_INVALIDPARAMS,"Data of BatchPage must be a positive integer. It representing the LOD level this detail level stores.","BatchPage::BatchPage");
#endif
	mLODLevel = datacast; 

	sceneMgr = geom->getSceneManager();
	batch = new BatchedGeometry(sceneMgr, geom->getSceneNode());

	fadeEnabled = false;

	if(!geom->getShadersEnabled())
	{
		// shaders disabled by config
		shadersSupported = false;
	} else
	{
		// determine if shaders available
		const RenderSystemCapabilities *caps = Root::getSingleton().getRenderSystem()->getCapabilities();
		if (caps->hasCapability(RSC_VERTEX_PROGRAM))
			shadersSupported = true;
		else
			shadersSupported = false;
	}

	++refCount;
}

BatchPage::~BatchPage()
{
	delete batch;

	//Delete unfaded material references
	unfadedMaterials.clear();
}


void BatchPage::addEntity(Entity *ent, const Vector3 &position, const Quaternion &rotation, const Vector3 &scale, const Ogre::ColourValue &color)
{
	const size_t numManLod = ent->getNumManualLodLevels();

#ifdef _DEBUG
	//Warns if using LOD batch and entities does not have enough LOD support.
	if ( mLODLevel > 0 && numManLod < mLODLevel )
		Ogre::LogManager::getSingleton().logMessage( "BatchPage::addEntity: " + ent->getName() + " entity has less than " + Ogre::StringConverter::toString(mLODLevel) + " manual lod level(s). Performance warning.");
#endif

	if (mLODLevel == 0 || numManLod == 0) 
		batch->addEntity(ent, position, rotation, scale, color);
	else
	{
		const size_t bestLod = (numManLod<mLODLevel-1)?numManLod:(mLODLevel-1);
		Ogre::Entity * lod = ent->getManualLodLevel( bestLod );
		batch->addEntity(lod, position, rotation, scale, color);
	}
}

void BatchPage::build()
{
	batch->build();

	BatchedGeometry::SubBatchIterator it = batch->getSubBatchIterator();
	while (it.hasMoreElements()){
		BatchedGeometry::SubBatch *subBatch = it.getNext();
		MaterialPtr mat = subBatch->getMaterial();

		//Disable specular unless a custom shader is being used.
		//This is done because the default shader applied by BatchPage
		//doesn't support specular, and fixed-function needs to look
		//the same as the shader (for computers with no shader support)
		for (size_t t = 0; t < mat->getNumTechniques(); ++t){
			Technique *tech = mat->getTechnique(t);
			for (int p = 0; p < tech->getNumPasses(); ++p){
				Pass *pass = tech->getPass(p);
				if (pass->getVertexProgramName().empty())
					pass->setSpecular(0, 0, 0, 1);
			}
		}

		//Store the original materials
		unfadedMaterials.push_back(subBatch->getMaterial());
	}

	_updateShaders();
}

void BatchPage::removeEntities()
{
	batch->clear();

	unfadedMaterials.clear();
	fadeEnabled = false;
}


void BatchPage::setVisible(bool visible)
{
	batch->setVisible(visible);
}

void BatchPage::setFade(bool enabled, Real visibleDist, Real invisibleDist)
{
	if (!shadersSupported)
		return;

	//If fade status has changed...
	if (fadeEnabled != enabled)
	{
		fadeEnabled = enabled;

 		if (enabled)
		{
			//Transparent batches should render after impostors
			if(geom)
				batch->setRenderQueueGroup(geom->getRenderQueue());
			else
				batch->setRenderQueueGroup(RENDER_QUEUE_6);

 		} else {
 			//Opaque batches should render in the normal render queue
 			batch->setRenderQueueGroup(RENDER_QUEUE_MAIN);
 		}

		this->visibleDist = visibleDist;
		this->invisibleDist = invisibleDist;
		_updateShaders();
	}
}

void BatchPage::_updateShaders()
{
	if (!shadersSupported)
		return;

	uint32 i = 0;
	BatchedGeometry::SubBatchIterator it = batch->getSubBatchIterator();
	while (it.hasMoreElements()){
		BatchedGeometry::SubBatch *subBatch = it.getNext();
		MaterialPtr mat = unfadedMaterials[i++];

		//Check if lighting should be enabled
		bool lightingEnabled = false;
		for (unsigned short t = 0; t < mat->getNumTechniques(); ++t){
			Technique *tech = mat->getTechnique(t);
			for (unsigned short p = 0; p < tech->getNumPasses(); ++p){
				Pass *pass = tech->getPass(p);
				if (pass->getLightingEnabled()) {
					lightingEnabled = true;
					break;
				}
			}
			if (lightingEnabled)
				break;
		}

		//Compile the CG shader script based on various material / fade options
		Ogre::StringStream tmpName;
		tmpName << "BatchPage_";
		if (fadeEnabled)
			tmpName << "fade_";
		if (lightingEnabled)
			tmpName << "lit_";
		if (subBatch->vertexData->vertexDeclaration->findElementBySemantic(VES_DIFFUSE) != NULL)
			tmpName << "clr_";

		for (unsigned short i = 0; i < subBatch->vertexData->vertexDeclaration->getElementCount(); ++i)
		{
			const VertexElement *el = subBatch->vertexData->vertexDeclaration->getElement(i);
			if (el->getSemantic() == VES_TEXTURE_COORDINATES) {
				String uvType = "";
				switch (el->getType()) {
						case VET_FLOAT1: uvType = "1"; break;
						case VET_FLOAT2: uvType = "2"; break;
						case VET_FLOAT3: uvType = "3"; break;
						case VET_FLOAT4: uvType = "4"; break;
					default:
						//Do nothing
						break;
				}
				tmpName << uvType << '_';
			}
		}

		tmpName << "vp";

		const String vertexProgName = tmpName.str();

		String shaderLanguage = ShaderHelper::getShaderLanguage();

		//If the shader hasn't been created yet, create it
		if (!HighLevelGpuProgramManager::getSingleton().getByName(vertexProgName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME))
		{
			String vertexProgSource;

			if(shaderLanguage == "hlsl" || shaderLanguage == "cg")
			{

				vertexProgSource =
					"void main( \n"
					"	float4 iPosition : POSITION, \n"
					"	float3 normal    : NORMAL,	\n"
					"	out float4 oPosition : POSITION, \n";

				if (subBatch->vertexData->vertexDeclaration->findElementBySemantic(VES_DIFFUSE) != NULL) vertexProgSource +=
					"	float4 iColor    : COLOR, \n";

				unsigned texNum = 0;
				for (unsigned short i = 0; i < subBatch->vertexData->vertexDeclaration->getElementCount(); ++i) {
					const VertexElement *el = subBatch->vertexData->vertexDeclaration->getElement(i);
					if (el->getSemantic() == VES_TEXTURE_COORDINATES) {
						String uvType = "";
						switch (el->getType()) {
							case VET_FLOAT1: uvType = "float"; break;
							case VET_FLOAT2: uvType = "float2"; break;
							case VET_FLOAT3: uvType = "float3"; break;
							case VET_FLOAT4: uvType = "float4"; break;
							default:
								//Do nothing
								break;
						}

						vertexProgSource +=
						"	" + uvType + " iUV" + StringConverter::toString(texNum) + "			: TEXCOORD" + StringConverter::toString(texNum) + ",	\n"
						"	out " + uvType + " oUV" + StringConverter::toString(texNum) + "		: TEXCOORD" + StringConverter::toString(texNum) + ",	\n";

						++texNum;
					}
				}

				vertexProgSource +=
					"	out float oFog : FOG,	\n"
				"	out float4 oColor : COLOR, \n"
			    "	uniform float4 iFogParams,	\n";

				if (lightingEnabled) vertexProgSource +=
					"	uniform float4 objSpaceLight,	\n"
					"	uniform float4 lightDiffuse,	\n"
					"	uniform float4 lightAmbient,	\n";

				if (fadeEnabled) vertexProgSource +=
					"	uniform float3 camPos, \n";

				vertexProgSource +=
					"	uniform float4x4 worldViewProj,	\n"
					"	uniform float fadeGap, \n"
					"   uniform float invisibleDist )\n"
					"{	\n";

				if (lightingEnabled) {
					//Perform lighting calculations (no specular)
					vertexProgSource +=
					"	float3 light = normalize(objSpaceLight.xyz - (iPosition.xyz * objSpaceLight.w)); \n"
					"	float diffuseFactor = max(dot(normal, light), 0); \n";
					if (subBatch->vertexData->vertexDeclaration->findElementBySemantic(VES_DIFFUSE) != NULL)
						vertexProgSource += "oColor = (lightAmbient + diffuseFactor * lightDiffuse) * iColor; \n";
					else
						vertexProgSource += "oColor = (lightAmbient + diffuseFactor * lightDiffuse); \n";
				} else {
					if (subBatch->vertexData->vertexDeclaration->findElementBySemantic(VES_DIFFUSE) != NULL)
						vertexProgSource += "oColor = iColor; \n";
					else
						vertexProgSource += "oColor = float4(1, 1, 1, 1); \n";
				}

				if (fadeEnabled) vertexProgSource +=
					//Fade out in the distance
					"	float dist = distance(camPos.xz, iPosition.xz);	\n"
					"	oColor.a *= (invisibleDist - dist) / fadeGap;   \n";

				texNum = 0;
				for (unsigned short i = 0; i < subBatch->vertexData->vertexDeclaration->getElementCount(); ++i) {
					const VertexElement *el = subBatch->vertexData->vertexDeclaration->getElement(i);
					if (el->getSemantic() == VES_TEXTURE_COORDINATES) {
						vertexProgSource +=
						"	oUV" + StringConverter::toString(texNum) + " = iUV" + StringConverter::toString(texNum) + ";	\n";
						++texNum;
					}
				}
				vertexProgSource +=
					"	oPosition = mul(worldViewProj, iPosition);  \n";
				if (sceneMgr->getFogMode() == Ogre::FOG_EXP2) {
					vertexProgSource +=
						"	oFog = 1 - clamp (pow (2.71828, -oPosition.z * iFogParams.x), 0, 1); \n";
				} else {
					vertexProgSource +=
						"	oFog = oPosition.z; \n";
				}
				vertexProgSource += "}";
			}

			if(shaderLanguage == "glsl")
			{
				vertexProgSource =
					"uniform float fadeGap;        \n"
					"uniform float invisibleDist;   \n";

				if (lightingEnabled) vertexProgSource +=
					"uniform vec4 objSpaceLight;   \n"
					"uniform vec4 lightDiffuse;	   \n"
					"uniform vec4 lightAmbient;	   \n";

				if (fadeEnabled) vertexProgSource +=
					"uniform vec3 camPos;          \n";

				vertexProgSource +=
						"attribute vec4 uv0;\n"
						"varying vec4 oUv0;";

				vertexProgSource +=
					"void main() \n"
					"{ \n";

				if (lightingEnabled)
				{
					//Perform lighting calculations (no specular)
					vertexProgSource +=
					"   vec3 light = normalize(objSpaceLight.xyz - (gl_Vertex.xyz * objSpaceLight.w)); \n"
					"   float diffuseFactor = max(dot(gl_Normal, light), 0.0); \n";
					if (subBatch->vertexData->vertexDeclaration->findElementBySemantic(VES_DIFFUSE) != NULL)
					{
						vertexProgSource += "   gl_FrontColor = (lightAmbient + diffuseFactor * lightDiffuse) * gl_Color; \n";
					}
					else
					{
						vertexProgSource += "   gl_FrontColor = (lightAmbient + diffuseFactor * lightDiffuse); \n";
					}
				}
				else
				{
					if (subBatch->vertexData->vertexDeclaration->findElementBySemantic(VES_DIFFUSE) != NULL)
					{
						vertexProgSource += "   gl_FrontColor = gl_Color; \n";
					}
					else
					{
						vertexProgSource += "   gl_FrontColor = vec4(1.0, 1.0, 1.0, 1.0); \n";
					}
				}

				if (fadeEnabled)
				{
					vertexProgSource +=
					//Fade out in the distance
					"   float dist = distance(camPos.xz, gl_Vertex.xz);	\n"
					"   gl_FrontColor.a *= (invisibleDist - dist) / fadeGap;   \n";
				}



				vertexProgSource +=
					"	oUv0 = uv0;\n"
					"   gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;  \n";
				if (sceneMgr->getFogMode() == Ogre::FOG_EXP2) {
					vertexProgSource +=
						"	gl_FogFragCoord = clamp(exp(- gl_Fog.density * gl_Fog.density * gl_Position.z * gl_Position.z), 0.0, 1.0); \n";
				} else {
					vertexProgSource +=
						"	gl_FogFragCoord = gl_Position.z; \n";
				}

				vertexProgSource +=	"}";
			}


			HighLevelGpuProgramPtr vertexShader = HighLevelGpuProgramManager::getSingleton().createProgram(
				vertexProgName,
				ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
				shaderLanguage, GPT_VERTEX_PROGRAM);

			vertexShader->setSource(vertexProgSource);

			if (shaderLanguage == "hlsl")
			{
				vertexShader->setParameter("target", "vs_1_1");
				vertexShader->setParameter("entry_point", "main");
			}
			else if(shaderLanguage == "cg")
			{
				vertexShader->setParameter("profiles", "vs_1_1 arbvp1");
				vertexShader->setParameter("entry_point", "main");
			}
			// GLSL can only have one entry point "main".

			vertexShader->load();
			if (vertexShader->hasCompileError()) {
				OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, "Error loading the batching vertex shader.", "BatchPage::_updateShaders()");
			}
		}

		std::string fragmentProgramName("BatchFragStandard");
		String fragmentProgSource;
		//We also need a fragment program to go with our vertex program. Especially on ATI cards on Linux where we can't mix shaders and the fixed function pipeline.
		HighLevelGpuProgramPtr fragShader = static_cast<HighLevelGpuProgramPtr>(HighLevelGpuProgramManager::getSingleton().getByName(fragmentProgramName, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME));
		if (!fragShader){

			if (shaderLanguage == "glsl") {
				fragmentProgSource =
					"uniform sampler2D diffuseMap;\n"
					"varying vec4 oUv0;\n"
					"void main()	{\n"
					"	gl_FragColor = texture2D(diffuseMap, oUv0.st);\n"
					"	gl_FragColor.rgb = mix(gl_Fog.color, (gl_LightModel.ambient * gl_FragColor + gl_FragColor), gl_FogFragCoord).rgb;\n"
					"}";

			} else {
				fragmentProgSource = "void main \n"
					"( \n"
					"    float2				iTexcoord		: TEXCOORD0, \n"
					"	 float				iFog 			: FOG, \n"
					"	 out float4         oColour			: COLOR, \n"
					"    uniform sampler2D  diffuseTexture	: TEXUNIT0, \n"
					"    uniform float3		iFogColour \n"
					") \n"
					"{ \n"
					"	oColour = tex2D(diffuseTexture, iTexcoord.xy); \n"
					"   oColour.xyz = lerp(oColour.xyz, iFogColour, iFog);\n"
					"}";
			}

			fragShader = HighLevelGpuProgramManager::getSingleton().createProgram(
				fragmentProgramName,
				ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
				shaderLanguage, GPT_FRAGMENT_PROGRAM);

			fragShader->setSource(fragmentProgSource);

			if (shaderLanguage == "hlsl") {
				fragShader->setParameter("entry_point", "main");
				fragShader->setParameter("target", "ps_2_0");
			} else if (shaderLanguage == "cg") {
				fragShader->setParameter("profiles", "ps_2_0 arbfp1");
				fragShader->setParameter("entry_point", "main");
			}

			fragShader->load();
			if (fragShader->hasCompileError()) {
				OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, "Error loading the batching fragment shader.", "BatchPage::_updateShaders()");
			}
		}
		//Now that the shader is ready to be applied, apply it
		Ogre::StringStream materialSignature;
		materialSignature << "BatchMat|";
		materialSignature << mat->getName() << "|";
		if (fadeEnabled){
			materialSignature << visibleDist << "|";
			materialSignature << invisibleDist << "|";
		}

		//Search for the desired material
		MaterialPtr generatedMaterial = MaterialManager::getSingleton().getByName(materialSignature.str());
		if (!generatedMaterial){
			//Clone the material
			generatedMaterial = mat->clone(materialSignature.str());

			//And apply the fade shader
			for (Technique* tech : generatedMaterial->getSupportedTechniques()) {
				for (unsigned short p = 0; p < tech->getNumPasses(); ++p){
					Pass *pass = tech->getPass(p);

					//Setup vertex program
					if (pass->getVertexProgramName() == "")
						pass->setVertexProgram(vertexProgName);

					if (pass->getFragmentProgramName() == "" && fragShader->isSupported())
						pass->setFragmentProgram(fragmentProgramName);

					try{
						GpuProgramParametersSharedPtr params = pass->getVertexProgramParameters();
						params->setIgnoreMissingParams(true);

						if (lightingEnabled) {
							params->setNamedAutoConstant("objSpaceLight", GpuProgramParameters::ACT_LIGHT_POSITION_OBJECT_SPACE);
							params->setNamedAutoConstant("lightDiffuse", GpuProgramParameters::ACT_DERIVED_LIGHT_DIFFUSE_COLOUR);
							params->setNamedAutoConstant("lightAmbient", GpuProgramParameters::ACT_DERIVED_AMBIENT_LIGHT_COLOUR);
							//params->setNamedAutoConstant("matAmbient", GpuProgramParameters::ACT_SURFACE_AMBIENT_COLOUR);
						}

						if (shaderLanguage.compare("glsl") == 0) {
							//glsl can use the built in gl_ModelViewProjectionMatrix
							params->setNamedAutoConstant("worldViewProj", GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX);
						} else {
							params->setNamedAutoConstant("iFogParams", GpuProgramParameters::ACT_FOG_PARAMS);
						}

						if (fadeEnabled)
						{
							params->setNamedAutoConstant("camPos", GpuProgramParameters::ACT_CAMERA_POSITION_OBJECT_SPACE);

							//Set fade ranges
							params->setNamedAutoConstant("invisibleDist", GpuProgramParameters::ACT_CUSTOM);
							params->setNamedConstant("invisibleDist", invisibleDist);

							params->setNamedAutoConstant("fadeGap", GpuProgramParameters::ACT_CUSTOM);
							params->setNamedConstant("fadeGap", invisibleDist - visibleDist);

							if (pass->getAlphaRejectFunction() == CMPF_ALWAYS_PASS)
								pass->setSceneBlending(SBT_TRANSPARENT_ALPHA);
						}

						if (pass->hasFragmentProgram()) {
							params = pass->getFragmentProgramParameters();
							params->setIgnoreMissingParams(true);
							params->setNamedAutoConstant("iFogColour", GpuProgramParameters::ACT_FOG_COLOUR);
						}
					}
					catch (...) {
						OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, "Error configuring batched geometry transitions. If you're using materials with custom vertex shaders, they will need to implement fade transitions to be compatible with BatchPage.", "BatchPage::_updateShaders()");
					}
				}
			}

		}

		//Apply the material
		subBatch->setMaterial(generatedMaterial);
	}

}
}
