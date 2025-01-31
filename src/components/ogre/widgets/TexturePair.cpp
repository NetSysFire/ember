/*
 Copyright (C) 2010 erik

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software Foundation,
 Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "TexturePair.h"

namespace Ember
{
namespace OgreView
{

namespace Gui
{

TexturePair::TexturePair(const Ogre::TexturePtr& _ogreTexture, const CEGUI::Image* _textureImage) :
		ogreTexture(_ogreTexture), textureImage(_textureImage)
{
}

TexturePair::TexturePair() :
		textureImage(nullptr)
{
}

bool TexturePair::hasData() const
{
	return textureImage != nullptr;
}

}

}
}
