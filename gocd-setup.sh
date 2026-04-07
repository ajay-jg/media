#!/bin/bash
PATH=$PATH:/home/go/.gem/ruby/2.5.0/bin
gem install --user-install fastlane-plugin-slack_upload
git config --global user.email "gocd@hotstar.com"
git config --global user.name "GOCD"