#!/bin/bash

if [ -x "$(command -v greadlink)" ]; then
	dir=$(dirname "$(greadlink -f "$0")")
else
	dir=$(dirname "$(readlink -f "$0")")
fi

commit=$(git log -n 1 --pretty=format:"%ci %H" | tr : - | tr \  _)


echo "Uploading AlloUnity to Unity rendering machine ..."
#ssh -p 60001 localhost "if not exist Desktop\\AlloUnity\\$commit mkdir Desktop\\AlloUnity\\$commit" > /dev/null
#scp -P 60001 -r ${dir}/../Bin/* localhost:Desktop/AlloUnity/$commit/
#ssh -p 60001 localhost "rmdir Desktop\\AlloUnity\\latest && mklink /D Desktop\\AlloUnity\\latest $commit" > /dev/null
echo "Done!"
echo "Uploading Unity builds to Unity rendering machine ..."
ssh -p 60001 localhost "if not exist \"Desktop\\Unity\\ Builds\\$commit\" mkdir \"Desktop\\Unity Builds\\$commit\"" > /dev/null
scp -P 60001 -r ${dir}/../../Unity\ Builds/* localhost:Desktop/Unity\\\ Builds/$commit/
ssh -p 60001 localhost "rmdir \"Desktop\\Unity Builds\\latest\" && mklink /D \"Desktop\\Unity Builds\\latest\" $commit" > /dev/null
echo "Done!"