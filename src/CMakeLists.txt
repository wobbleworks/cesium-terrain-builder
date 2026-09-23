# Ensure we have a unified GDAL: `GDALOpenEx` is only present after
# unification.
# Note: Check disabled - modern GDAL (>= 2.0) always has this
set(HAVE_UNIFIED_GDAL TRUE)

# We need zlib
find_package(ZLIB REQUIRED)

add_library(ctb SHARED
  GDALTile.cpp
  GDALTiler.cpp
  GDALDatasetReader.cpp
  CTBFileTileSerializer.cpp
  CTBFileOutputStream.cpp
  CTBZOutputStream.cpp
  TerrainTiler.cpp
  TerrainTile.cpp
  MeshTiler.cpp
  MeshTile.cpp
  GlobalMercator.cpp
  GlobalGeodetic.cpp)
target_link_libraries(ctb PUBLIC GDAL::GDAL PRIVATE ZLIB::ZLIB)
target_compile_definitions(ctb PRIVATE CTB_EXPORTS)

# Install libctb
set(HEADERS
  Bounds.hpp
  BoundingSphere.hpp
  Coordinate.hpp
  Coordinate3D.hpp
  GDALSerializer.hpp
  GDALTile.hpp
  GDALTiler.hpp
  GDALDatasetReader.hpp
  CTBFileTileSerializer.hpp
  CTBFileOutputStream.hpp
  CTBOutputStream.hpp
  CTBZOutputStream.hpp
  GlobalGeodetic.hpp
  GlobalMercator.hpp
  Grid.hpp
  GridIterator.hpp
  HeightFieldChunker.hpp
  Mesh.hpp
  MeshIterator.hpp
  MeshSerializer.hpp
  MeshTile.hpp
  MeshTiler.hpp
  RasterIterator.hpp
  RasterTiler.hpp
  CTBException.hpp
  TerrainIterator.hpp
  TerrainSerializer.hpp
  TerrainTile.hpp
  TerrainTiler.hpp
  Tile.hpp
  TileCoordinate.hpp
  TilerIterator.hpp
  types.hpp)
install(FILES ${HEADERS} DESTINATION include/ctb)
install(FILES ctb.hpp DESTINATION include)

install(TARGETS ctb RUNTIME DESTINATION bin LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
