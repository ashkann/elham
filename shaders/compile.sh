#!/usr/bin/env sh
GLSLC=`which glslc`
$GLSLC shader.vert -o vert.spv
$GLSLC shader.frag -o frag.spv
$GLSLC ycbcr.comp -o ycbcr.spv