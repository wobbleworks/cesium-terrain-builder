/*******************************************************************************
 * Copyright 2018 GeoData <geodata@soton.ac.uk>
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *******************************************************************************/

/**
 * @file MeshTiler.cpp
 * @brief This defines the `MeshTiler` class
 * @author Alvaro Huarte <ahuarte47@yahoo.es>
 */

#include "cpl_conv.h"
#include "gdal_priv.h"

#include "CTBException.hpp"
#include "MeshTiler.hpp"
#include "HeightFieldChunker.hpp"
#include "GDALDatasetReader.hpp"
#include "GDALTile.hpp"

using namespace ctb;

/**
 * @brief Create a raster tile with vertex-centered sampling for mesh generation.
 *
 * Unlike TerrainTiler::createRasterTile (which adds a 1-pixel overlap for the
 * heightmap format), this places pixel centers exactly at the mesh vertex
 * positions.  The mesh has tileSize vertices spanning (tileSize-1) intervals
 * across the tile, so the pixel spacing must be tileWidth/(tileSize-1) rather
 * than the grid resolution tileWidth/tileSize.
 */
GDALTile *
ctb::MeshTiler::createRasterTile(GDALDataset *dataset, const TileCoordinate &coord) const {
  if (dataset && dataset->GetRasterCount() < 1) {
    throw CTBException("At least one band must be present in the GDAL dataset");
  }

  const i_tile tileSize = mGrid.tileSize();
  CRSBounds tileBounds = mGrid.tileBounds(coord);

  // Vertex spacing: tileSize vertices span (tileSize-1) intervals
  double cellSizeX = tileBounds.getWidth()  / (double)(tileSize - 1);
  double cellSizeY = tileBounds.getHeight() / (double)(tileSize - 1);

  // Shift the origin so that GDAL pixel centers (at col+0.5) land on vertex
  // positions: pixel 0 center = tileBounds min, pixel N-1 center = tileBounds max.
  double adfGeoTransform[6];
  adfGeoTransform[0] = tileBounds.getMinX() - 0.5 * cellSizeX;
  adfGeoTransform[1] = cellSizeX;
  adfGeoTransform[2] = 0;
  adfGeoTransform[3] = tileBounds.getMaxY() + 0.5 * cellSizeY;
  adfGeoTransform[4] = 0;
  adfGeoTransform[5] = -cellSizeY;

  GDALTile *tile = GDALTiler::createRasterTile(dataset, adfGeoTransform);
  static_cast<TileCoordinate &>(*tile) = coord;

  return tile;
}

////////////////////////////////////////////////////////////////////////////////

/**
 * Implementation of ctb::chunk::mesh for ctb::Mesh class.
 */
class WrapperMesh : public ctb::chunk::mesh {
private:
  CRSBounds &mBounds;
  Mesh &mMesh;
  double mCellSizeX;
  double mCellSizeY;

  std::map<int, int> mIndicesMap;
  Coordinate<int> mTriangles[3];
  bool mTriOddOrder;
  int mTriIndex;

public:
  WrapperMesh(CRSBounds &bounds, Mesh &mesh, i_tile tileSizeX, i_tile tileSizeY):
    mMesh(mesh),
    mBounds(bounds),
    mTriOddOrder(false),
    mTriIndex(0) {
    mCellSizeX = (bounds.getMaxX() - bounds.getMinX()) / (double)(tileSizeX - 1);
    mCellSizeY = (bounds.getMaxY() - bounds.getMinY()) / (double)(tileSizeY - 1);
  }

  virtual void clear() {
    mMesh.vertices.clear();
    mMesh.indices.clear();
    mIndicesMap.clear();
    mTriOddOrder = false;
    mTriIndex = 0;
  }
  virtual void emit_vertex(const ctb::chunk::heightfield &heightfield, int x, int y) {
    mTriangles[mTriIndex].x = x;
    mTriangles[mTriIndex].y = y;
    mTriIndex++;

    if (mTriIndex == 3) {
      mTriOddOrder = !mTriOddOrder;

      if (mTriOddOrder) {
        appendVertex(heightfield, mTriangles[0].x, mTriangles[0].y);
        appendVertex(heightfield, mTriangles[1].x, mTriangles[1].y);
        appendVertex(heightfield, mTriangles[2].x, mTriangles[2].y);
      }
      else {
        appendVertex(heightfield, mTriangles[1].x, mTriangles[1].y);
        appendVertex(heightfield, mTriangles[0].x, mTriangles[0].y);
        appendVertex(heightfield, mTriangles[2].x, mTriangles[2].y);
      }
      mTriangles[0].x = mTriangles[1].x;
      mTriangles[0].y = mTriangles[1].y;
      mTriangles[1].x = mTriangles[2].x;
      mTriangles[1].y = mTriangles[2].y;
      mTriIndex--;
    }
  }
  void appendVertex(const ctb::chunk::heightfield &heightfield, int x, int y) {
    int iv;
    int index = heightfield.indexOfGridCoordinate(x, y);

    std::map<int, int>::iterator it = mIndicesMap.find(index);

    if (it == mIndicesMap.end()) {
      iv = mMesh.vertices.size();

      double xmin = mBounds.getMinX();
      double ymax = mBounds.getMaxY();
      double height = heightfield.height(x, y);

      mMesh.vertices.push_back(CRSVertex(xmin + (x * mCellSizeX), ymax - (y * mCellSizeY), height));
      mIndicesMap.insert(std::make_pair(index, iv));
    }
    else {
      iv = it->second;
    }
    mMesh.indices.push_back(iv);
  }
};

////////////////////////////////////////////////////////////////////////////////

void 
ctb::MeshTiler::prepareSettingsOfTile(MeshTile *terrainTile, GDALDataset *dataset, const TileCoordinate &coord, float *rasterHeights, ctb::i_tile tileSizeX, ctb::i_tile tileSizeY) const {
  const ctb::i_tile TILE_SIZE = tileSizeX;

  // Number of tiles in the horizontal direction at tile level zero.
  double resolutionAtLevelZero = mGrid.resolution(0);
  int numberOfTilesAtLevelZero = (int)(mGrid.getExtent().getWidth() / (tileSizeX * resolutionAtLevelZero));
  // Default quality of terrain created from heightmaps (TerrainProvider.js).
  double heightmapTerrainQuality = 0.25;
  // Ellipsoid semi-major-axis in meters (configurable via setEllipsoidRadii).
  const double semiMajorAxis = ctb::getEquatorialRadius();
  // Appropriate geometric error estimate when the geometry comes from a heightmap (TerrainProvider.js).
  double maximumGeometricError = MeshTiler::getEstimatedLevelZeroGeometricErrorForAHeightmap(
    semiMajorAxis,
    heightmapTerrainQuality * mMeshQualityFactor,
    TILE_SIZE,
    numberOfTilesAtLevelZero
  );
  // Geometric error for current Level.
  maximumGeometricError /= (double)(1 << coord.zoom);

  // Convert the raster grid into an irregular mesh applying the Chunked LOD strategy by 'Thatcher Ulrich'.
  // http://tulrich.com/geekstuff/chunklod.html
  //
  ctb::chunk::heightfield heightfield(rasterHeights, TILE_SIZE);
  heightfield.applyGeometricError(maximumGeometricError, coord.zoom <= 6);
  //
  // Propagate the geometric error of neighbours to avoid gaps in borders.
  if (coord.zoom > 6) {
    ctb::CRSBounds datasetBounds = bounds();

    for (int borderIndex = 0; borderIndex < 4; borderIndex++) {
      bool okNeighborCoord = true;
      ctb::TileCoordinate neighborCoord = ctb::chunk::heightfield::neighborCoord(mGrid, coord, borderIndex, okNeighborCoord);
      if (!okNeighborCoord)
        continue;

      ctb::CRSBounds neighborBounds = mGrid.tileBounds(neighborCoord);

      if (datasetBounds.overlaps(neighborBounds)) {
        float *neighborHeights = ctb::GDALDatasetReader::readRasterHeights(*this, dataset, neighborCoord, mGrid.tileSize(), mGrid.tileSize());

        ctb::chunk::heightfield neighborHeightfield(neighborHeights, TILE_SIZE);
        neighborHeightfield.applyGeometricError(maximumGeometricError);
        heightfield.applyBorderActivationState(neighborHeightfield, borderIndex);

        CPLFree(neighborHeights);
      }
    }
  }
  ctb::CRSBounds mGridBounds = mGrid.tileBounds(coord);
  Mesh &tileMesh = terrainTile->getMesh();
  WrapperMesh mesh(mGridBounds, tileMesh, tileSizeX, tileSizeY);
  heightfield.generateMesh(mesh, 0);
  heightfield.clear();

  // If we are not at the maximum zoom level we need to set child flags on the
  // tile where child tiles overlap the dataset bounds.
  if (coord.zoom != maxZoomLevel()) {
    CRSBounds tileBounds = mGrid.tileBounds(coord);

    if (! (bounds().overlaps(tileBounds))) {
      terrainTile->setAllChildren(false);
    } else {
      if (bounds().overlaps(tileBounds.getSW())) {
        terrainTile->setChildSW();
      }
      if (bounds().overlaps(tileBounds.getNW())) {
        terrainTile->setChildNW();
      }
      if (bounds().overlaps(tileBounds.getNE())) {
        terrainTile->setChildNE();
      }
      if (bounds().overlaps(tileBounds.getSE())) {
        terrainTile->setChildSE();
      }
    }
  }
}

MeshTile *
ctb::MeshTiler::createMesh(GDALDataset *dataset, const TileCoordinate &coord) const {
  // Copy the raster data into an array
  float *rasterHeights = ctb::GDALDatasetReader::readRasterHeights(*this, dataset, coord, mGrid.tileSize(), mGrid.tileSize());

  // Get a mesh tile represented by the tile coordinate
  MeshTile *terrainTile = new MeshTile(coord);
  prepareSettingsOfTile(terrainTile, dataset, coord, rasterHeights, mGrid.tileSize(), mGrid.tileSize());
  CPLFree(rasterHeights);

  if (mComputeGhostNormals) {
    readAndSetExtendedHeights(terrainTile, dataset, coord);
  }

  return terrainTile;
}

MeshTile *
ctb::MeshTiler::createMesh(GDALDataset *dataset, const TileCoordinate &coord, ctb::GDALDatasetReader *reader) const {
  // Copy the raster data into an array
  float *rasterHeights = reader->readRasterHeights(dataset, coord, mGrid.tileSize(), mGrid.tileSize());

  // Get a mesh tile represented by the tile coordinate
  MeshTile *terrainTile = new MeshTile(coord);
  prepareSettingsOfTile(terrainTile, dataset, coord, rasterHeights, mGrid.tileSize(), mGrid.tileSize());
  CPLFree(rasterHeights);

  if (mComputeGhostNormals) {
    readAndSetExtendedHeights(terrainTile, dataset, coord);
  }

  return terrainTile;
}

MeshTiler &
ctb::MeshTiler::operator=(const MeshTiler &other) {
  TerrainTiler::operator=(other);
  mMeshQualityFactor = other.mMeshQualityFactor;
  mComputeGhostNormals = other.mComputeGhostNormals;

  return *this;
}

double ctb::MeshTiler::getEstimatedLevelZeroGeometricErrorForAHeightmap(
  double maximumRadius,
  double heightmapTerrainQuality,
  int tileWidth,
  int numberOfTilesAtLevelZero)
{
  double error = maximumRadius * 2 * M_PI * heightmapTerrainQuality;
  error /= (double)(tileWidth * numberOfTilesAtLevelZero);
  return error;
}

void
ctb::MeshTiler::readAndSetExtendedHeights(MeshTile *tile, GDALDataset *dataset, const TileCoordinate &coord) const {
  const int tileSize = mGrid.tileSize();
  const int extendedSize = tileSize + 2; // 67 for a 65-pixel tile

  // Use the same vertex spacing as the mesh
  CRSBounds tileBounds = mGrid.tileBounds(coord);
  double cellSizeX = tileBounds.getWidth()  / (double)(tileSize - 1);
  double cellSizeY = tileBounds.getHeight() / (double)(tileSize - 1);

  // Extend by 1 vertex spacing on all 4 sides
  double extMinX = tileBounds.getMinX() - cellSizeX;
  double extMinY = tileBounds.getMinY() - cellSizeY;
  double extMaxX = tileBounds.getMaxX() + cellSizeX;
  double extMaxY = tileBounds.getMaxY() + cellSizeY;
  CRSBounds extBounds(extMinX, extMinY, extMaxX, extMaxY);

  // Vertex-centered geo-transform: pixel centers at vertex positions
  double adfGeoTransform[6];
  adfGeoTransform[0] = extMinX - 0.5 * cellSizeX;
  adfGeoTransform[1] = cellSizeX;
  adfGeoTransform[2] = 0;
  adfGeoTransform[3] = extMaxY + 0.5 * cellSizeY;
  adfGeoTransform[4] = 0;
  adfGeoTransform[5] = -cellSizeY;

  // Create a VRT for the extended area
  GDALTile *vrtTile = NULL;
  try {
    vrtTile = GDALTiler::createRasterTile(dataset, adfGeoTransform, extendedSize, extendedSize);
  } catch (...) {
    // VRT creation failed; fall back to triangle-based normals
    return;
  }

  if (vrtTile == NULL || vrtTile->dataset == NULL) {
    delete vrtTile;
    return;
  }

  // Read the extended heights via RasterIO
  float *extHeights = (float *)CPLMalloc(sizeof(float) * extendedSize * extendedSize);
  GDALRasterBand *band = ((GDALDataset *)vrtTile->dataset)->GetRasterBand(1);

  if (band == NULL ||
      band->RasterIO(GF_Read, 0, 0, extendedSize, extendedSize,
                     extHeights, extendedSize, extendedSize,
                     GDT_Float32, 0, 0) != CE_None) {
    CPLFree(extHeights);
    delete vrtTile;
    return;
  }

  // Get NoData value
  int bGotNoData = FALSE;
  double noDataValue = band->GetNoDataValue(&bGotNoData);
  if (!bGotNoData) noDataValue = -32768;

  delete vrtTile;

  // --- Antimeridian wrapping for ghost columns ---
  // At the antimeridian (±180°), the ghost column extends beyond the raster
  // and reads NoData. Fix by reading the wrapped column from the opposite edge.
  {
    ctb::TileBounds extent = mGrid.getTileExtent(coord.zoom);
    i_tile maxTileX = extent.getMaxX();

    if (coord.x == 0 || coord.x == maxTileX) {
      for (int side = 0; side < 2; side++) {
        bool doWrap = (side == 0) ? (coord.x == 0) : (coord.x == maxTileX);
        if (!doWrap) continue;

        // side 0 = west ghost col (col 0), side 1 = east ghost col (col N-1)
        int targetCol = (side == 0) ? 0 : (extendedSize - 1);
        double ghostLon = (side == 0) ? extMinX : extMaxX;
        double wrapLon = ghostLon + ((side == 0) ? 360.0 : -360.0);

        double wrapGT[6];
        wrapGT[0] = wrapLon - 0.5 * cellSizeX;
        wrapGT[1] = cellSizeX;
        wrapGT[2] = 0;
        wrapGT[3] = extMaxY + 0.5 * cellSizeY;
        wrapGT[4] = 0;
        wrapGT[5] = -cellSizeY;

        GDALTile *wrapTile = NULL;
        try {
          wrapTile = GDALTiler::createRasterTile(dataset, wrapGT, 1, extendedSize);
        } catch (...) {
          continue;
        }
        if (!wrapTile || !wrapTile->dataset) {
          delete wrapTile;
          continue;
        }

        float *wrapCol = (float *)CPLMalloc(sizeof(float) * extendedSize);
        GDALRasterBand *wb = ((GDALDataset *)wrapTile->dataset)->GetRasterBand(1);
        if (wb && wb->RasterIO(GF_Read, 0, 0, 1, extendedSize,
                               wrapCol, 1, extendedSize,
                               GDT_Float32, 0, 0) == CE_None) {
          for (int row = 0; row < extendedSize; row++) {
            if (wrapCol[row] != (float)noDataValue) {
              extHeights[row * extendedSize + targetCol] = wrapCol[row];
            }
          }
        }
        CPLFree(wrapCol);
        delete wrapTile;
      }
    }
  }

  // Replace remaining NoData values with nearest interior edge value (flat extrapolation).
  // After antimeridian wrapping, only polar ghost rows should still have NoData.
  for (int row = 0; row < extendedSize; row++) {
    for (int col = 0; col < extendedSize; col++) {
      float &h = extHeights[row * extendedSize + col];
      if (h == (float)noDataValue) {
        // Clamp to nearest interior cell
        int srcRow = std::max(1, std::min(row, extendedSize - 2));
        int srcCol = std::max(1, std::min(col, extendedSize - 2));
        float srcH = extHeights[srcRow * extendedSize + srcCol];

        // If the clamped source is also NoData, try the tile center
        if (srcH == (float)noDataValue) {
          srcH = extHeights[(extendedSize / 2) * extendedSize + (extendedSize / 2)];
        }
        // If still NoData, use 0
        if (srcH == (float)noDataValue) {
          srcH = 0.0f;
        }
        h = srcH;
      }
    }
  }

  tile->setExtendedHeights(extHeights, extendedSize, extBounds, tileBounds);
}
