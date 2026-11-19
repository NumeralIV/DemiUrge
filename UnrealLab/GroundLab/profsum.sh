#!/bin/sh
# profsum.sh <log>: print the main GPU passes from a ProfileGPU dump (inclusive ms of the second column block).
grep "LogRHI: Display:" "$1" | grep -E "┃ +(Scene|VolumetricCloudShadow|VolumetricCloud|BasePass|Lumen[A-Za-z]*|PostProcessing|SkyAtmosphere|ShadowDepths|Lights|DiffuseIndirectAndAO|ReflectionEnvironmentAndSky|TSR[A-Za-z]*|Translucency|VolCloud[A-Za-z]*) " | awk -F'┃' '{split($3,a,"│"); n=$4; gsub(/^ +| +$/,"",n); m=a[6]; sub(/^.*┊ */,"",m); print n": "m}' | head -30
