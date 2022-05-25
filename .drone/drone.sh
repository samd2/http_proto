#!/bin/bash

# Copyright 2020 Rene Rivera, Sam Darwin
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE.txt or copy at http://boost.org/LICENSE_1_0.txt)

set -xe

export DRONE_BUILD_DIR=$(pwd)
export VCS_COMMIT_ID=$DRONE_COMMIT
export GIT_COMMIT=$DRONE_COMMIT
export REPO_NAME=$DRONE_REPO
export USER=$(whoami)
export CC=${CC:-gcc}
export PATH=~/.local/bin:/usr/local/bin:$PATH
export TRAVIS_BUILD_DIR=$(pwd)
export TRAVIS_BRANCH=$DRONE_BRANCH
export TRAVIS_EVENT_TYPE=$DRONE_BUILD_EVENT

common_install () {
  if [ -z "$SELF" ]; then
    export SELF=`basename $REPO_NAME`
  fi

  git clone https://github.com/boostorg/boost-ci.git boost-ci-cloned --depth 1
  [ "$SELF" == "boost-ci" ] || cp -prf boost-ci-cloned/ci .
  rm -rf boost-ci-cloned

  if [ "$TRAVIS_OS_NAME" == "osx" ]; then
      unset -f cd
  fi

  export BOOST_CI_TARGET_BRANCH="$TRAVIS_BRANCH"
  export BOOST_CI_SRC_FOLDER=$(pwd)

  . ./ci/common_install.sh

  # Customizations
  pwd
  pushd $BOOST_ROOT/libs
  git clone https://github.com/CPPAlliance/url -b develop
  popd
  pwd
}

if [ "$DRONE_JOB_BUILDTYPE" == "boost" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> SCRIPT'

. $BOOST_ROOT/libs/$SELF/ci/build.sh

elif [ "$DRONE_JOB_BUILDTYPE" == "codecov" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> SCRIPT'

cd $BOOST_ROOT/libs/$SELF
ci/travis/codecov.sh

elif [ "$DRONE_JOB_BUILDTYPE" == "valgrind" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> SCRIPT'

cd $BOOST_ROOT/libs/$SELF
ci/travis/valgrind.sh

elif [ "$DRONE_JOB_BUILDTYPE" == "coverity" ]; then

echo '==================================> INSTALL'

common_install

echo '==================================> SCRIPT'

if  [ -n "${COVERITY_SCAN_NOTIFICATION_EMAIL}" -a \( "$TRAVIS_BRANCH" = "develop" -o "$TRAVIS_BRANCH" = "master" \) -a \( "$DRONE_BUILD_EVENT" = "push" -o "$DRONE_BUILD_EVENT" = "cron" \) ] ; then
cd $BOOST_ROOT/libs/$SELF
ci/travis/coverity.sh
fi

elif [ "$DRONE_JOB_BUILDTYPE" == "cmake1" ]; then

set -xe

echo '==================================> INSTALL'

# already in the image
# pip install --user cmake

echo '==================================> SCRIPT'

export SELF=`basename $REPO_NAME`
BOOST_BRANCH=develop && [ "$DRONE_BRANCH" == "master" ] && BOOST_BRANCH=master || true
echo BOOST_BRANCH: $BOOST_BRANCH
cd ..
git clone -b $BOOST_BRANCH --depth 1 https://github.com/boostorg/boost.git boost-root
cd boost-root

mkdir -p libs/$SELF
cp -r $DRONE_BUILD_DIR/* libs/$SELF
# git submodule update --init tools/boostdep
git submodule update --init --recursive

# Customizations
if [ ! -d "libs/url" ]; then
  cd libs
  git clone https://github.com/CPPAlliance/url -b develop
  cd ..
fi

cd libs/$SELF
mkdir __build__ && cd __build__
cmake -DCMAKE_INSTALL_PREFIX=~/.local ..
cmake --build . --target install
#
# cmake_install_test directory was removed. So the following test would fail.
# cd ../test/cmake_install_test && mkdir __build__ && cd __build__
# cmake -DCMAKE_INSTALL_PREFIX=~/.local ..
# cmake --build .
# cmake --build . --target check

fi
