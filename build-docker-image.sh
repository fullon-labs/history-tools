#!/bin/bash

VERSION_STRING=$(git describe --tags --dirty)
DOCKER_IMG=${DOCKER_IMG:-"fullon/history-tools"}
VERSION=${VERSION:-"0.5.0-alpha1"}

docker build -t ${DOCKER_IMG}:$VERSION --build-arg VERSION_STRING=${VERSION_STRING} $@ .
