#pragma once

#include "map.h"
#include "layer.h"
#include "tilelayer.h"

namespace Tiled {

class Workspace
{
public:
	Workspace(int width, int height, int tileWidth, int tileHeight)
		: mWidth(width)
		, mHeight(height)
		, mTileWidth(tileWidth)
		, mTileHeight(tileHeight)
	{}

	Workspace()
		: mWidth(0)
		, mHeight(0)
		, mTileWidth(0)
		, mTileHeight(0)
	{}

	 int width() const { return mWidth; }
	 int height() const { return mHeight; }
	 int tileWidth() const { return mTileWidth; }
	 int tileHeight() const { return mTileHeight; }
	 QSize tileSize() const { return QSize(mWidth, mHeight); }

	 void setWidth(const int width) { mWidth = width; }
	 void setHeight(const int height) { mHeight = height; }
	 void setTileWidth(const int tileWidth) { mTileWidth = tileWidth; }
	 void setTileHeight(const int tileHeight) { mTileHeight = tileHeight; }
private:
	  int mWidth;
	  int mHeight;
	  int mTileWidth;
	  int mTileHeight;
};

}
