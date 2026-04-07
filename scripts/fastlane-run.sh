#!/bin/bash

set -e
PATH=$PATH:/usr/local/bin

function runLane() {
   cd .. && fastlane android $1
}

export LC_ALL=en_US.UTF-8
export LANG=en_US.UTF-8
runLane $1