#!/bin/bash
# Installs the Linux packages JUCE needs so cloud sessions can build, validate and snapshot the plugin.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

missing=()
for pkg in libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxext-dev libxinerama-dev \
           libxrandr-dev libxcursor-dev libcurl4-openssl-dev ninja-build xvfb; do
  dpkg -s "$pkg" >/dev/null 2>&1 || missing+=("$pkg")
done

if [ ${#missing[@]} -gt 0 ]; then
  apt-get update -qq >/dev/null
  DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${missing[@]}" >/dev/null
fi

if [ ! -x /tmp/pluginval/pluginval ]; then
  mkdir -p /tmp/pluginval
  curl -sSL -o /tmp/pluginval/pluginval.zip https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Linux.zip
  unzip -oq /tmp/pluginval/pluginval.zip -d /tmp/pluginval
fi
