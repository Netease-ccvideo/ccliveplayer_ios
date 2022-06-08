#!/bin/bash

set -e

# xcode-select -switch  /Users/cc/Downloads/Xcode.app

# IOSSDK_VER="11.4"
BUILD_BRANCH=$1

BUILD_TAG=$2

echo $BUILD_BRANCH
echo $BUILD_TAG

GIT_COMMIT_HASH=$(eval git log --pretty=format:'%h' -n 1)

echo $GIT_COMMIT_HASH

dirname $0

cd `dirname $0`

PROJECT_NAME="MLiveCCPlayer"

UNIVERSAL_OUTPUTFOLDER="universal"

CONFIGURATION="Release"

# make sure the output directory exists
OUTPUTDIR="../Release"
mkdir -p Release/
# rm -rf Release/*

# xcodebuild -showsdks
cd MLiveCCPlayer

BUILD_PATH="build"

echo "----------Begin------------"
# Step 1. xcode build
xcodebuild -project ${PROJECT_NAME}.xcodeproj -scheme ${PROJECT_NAME} -configuration ${CONFIGURATION} -sdk iphoneos${IOSSDK_VER} clean
xcodebuild -project ${PROJECT_NAME}.xcodeproj -scheme ${PROJECT_NAME} -configuration ${CONFIGURATION} -sdk iphonesimulator${IOSSDK_VER} clean
xcodebuild -project ${PROJECT_NAME}.xcodeproj -scheme ${PROJECT_NAME} -configuration ${CONFIGURATION} -sdk iphoneos${IOSSDK_VER}  build -derivedDataPath ${BUILD_PATH}
xcodebuild -project ${PROJECT_NAME}.xcodeproj -scheme ${PROJECT_NAME} -configuration ${CONFIGURATION} -sdk iphonesimulator${IOSSDK_VER}  build -derivedDataPath ${BUILD_PATH}

mkdir -p ${UNIVERSAL_OUTPUTFOLDER}
rm -rf ${UNIVERSAL_OUTPUTFOLDER}/*

BUILD_PRODUCT_DIR=${BUILD_PATH}/Build/Products
# Step 2. Copy the framework structure (from iphoneos build) to the universal folder
cp -R ${BUILD_PRODUCT_DIR}/${CONFIGURATION}-iphoneos/* ${UNIVERSAL_OUTPUTFOLDER}
# Step 3. Create universal binary file using lipo and place the combined executable in the copied framework directory
lipo -create "${BUILD_PRODUCT_DIR}/${CONFIGURATION}-iphonesimulator/${PROJECT_NAME}.framework/${PROJECT_NAME}" "${BUILD_PRODUCT_DIR}/${CONFIGURATION}-iphoneos/${PROJECT_NAME}.framework/${PROJECT_NAME}" -output "${UNIVERSAL_OUTPUTFOLDER}/${PROJECT_NAME}.framework/${PROJECT_NAME}"
echo "----------zip------------"
# Step 4. Convenience step to copy the framework to the project's directory
cp -R "${UNIVERSAL_OUTPUTFOLDER}/${PROJECT_NAME}.framework" "${OUTPUTDIR}"
# Step 5. Create .tar.gz file for posting on the binary repository
cd "${OUTPUTDIR}"
# We nest the framework inside a Frameworks folder so that it unarchives correctly
zip -r -q "${PROJECT_NAME}.framework_$BUILD_TAG-$BUILD_BRANCH-$GIT_COMMIT_HASH.zip" "./${PROJECT_NAME}.framework/"
#tar -zcf "${PROJECT_NAME}.framework.tar.gz" "${PROJECT_NAME}/Frameworks/"
#mv "${PROJECT_NAME}.framework.tar.gz" "${PROJECT_DIR}/"
echo "----------End------------"
# Step 6. Convenience step to open the project's directory in Finder
# open "${OUTPUTDIR}"
