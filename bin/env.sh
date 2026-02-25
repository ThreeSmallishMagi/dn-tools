#!/bin/bash
# add tools to path with . env
bin_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../bin" && pwd)
export PATH=$PATH:$bin_dir
base_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
env_dir=$(ls -d "$base_dir"/*env 2>/dev/null | head -1)
echo env_dir:$env_dir  bind_dir:$bin_dir
env_dir=$(cd "$env_dir" && pwd)
source $env_dir/*/activate
