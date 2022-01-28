set +e; unset -f cd; set -ex && for x in build-mksh build-lksh; do (cd $x && make -f GNUmakefile -j2 all); done
