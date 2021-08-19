#!/bin/bash

###################################################################################
# bletchmame_build_msys2.sh - Builds BletchMAME under MSYS2                       #
###################################################################################

# Sanity check
if [ -z "$BASH_SOURCE" ]; then
  echo "Null BASH_SOURCE"
  exit
fi

# Identify directories
BLETCHMAME_DIR=$(dirname $BASH_SOURCE)/..
BLETCHMAME_BUILD_DIR=build/msys2
BLETCHMAME_INSTALL_DIR=${BLETCHMAME_BUILD_DIR}
DEPS_INSTALL_DIR=$(dirname $BASH_SOURCE)/../deps/msys2

# Generate buildversion.txt
git describe --tags | python version.py | cat >buildversion.txt
echo "Build Version: $(cat buildversion.txt)"

# Generate src/buildversion.gen.cpp
echo >$BLETCHMAME_DIR/src/buildversion.gen.cpp  "const char buildVersion[] = \"`cat buildversion.txt 2>NUL`\";"
echo >>$BLETCHMAME_DIR/src/buildversion.gen.cpp "const char buildRevision[] = \"`git rev-parse HEAD 2>NUL`\";"
echo >>$BLETCHMAME_DIR/src/buildversion.gen.cpp "const char buildDateTime[] = \"`date -Ins`\";"

# Set up build directory
rm -rf ${BLETCHMAME_BUILD_DIR}
cmake -S. -B${BLETCHMAME_BUILD_DIR}												\
	-DUSE_SHARED_LIBS=off -DHAS_BUILDVERSION_GEN_CPP=1 							\
	-DQt6_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6									\
	-DQt6BundledFreetype_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6BundledFreetype	\
	-DQt6BundledHarfbuzz_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6BundledHarfbuzz	\
	-DQt6BundledLibpng_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6BundledLibpng		\
	-DQt6BundledPcre2_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6BundledPcre2			\
	-DQt6Core_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6Core							\
	-DQt6CoreTools_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6CoreTools				\
	-DQt6GuiTools_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6GuiTools					\
	-DQt6Scxml_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6Scxml						\
	-DQt6ScxmlTools_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6ScxmlTools				\
	-DQt6ScxmlQml_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6ScxmlQml					\
	-DQt6Qml_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6Qml							\
	-DQt6QmlTools_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6QmlTools					\
	-DQt6WidgetsTools_DIR=${DEPS_INSTALL_DIR}/lib/cmake/Qt6WidgetsTools			\
	-DQuaZip-Qt6_DIR=${DEPS_INSTALL_DIR}/lib/cmake/QuaZip-Qt6-1.1

# And build!
cmake --build ${BLETCHMAME_BUILD_DIR} --parallel
cmake --install ${BLETCHMAME_BUILD_DIR} --strip --prefix ${BLETCHMAME_INSTALL_DIR}