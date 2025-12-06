#!/bin/bash
x86_64-linux-gnu-g++ *.cpp *.h -o gxde-lsg-amd64 --static
aarch64-linux-gnu-g++ *.cpp *.h -o gxde-lsg-arm64 --static
mips64el-linux-gnuabi64-g++ *.cpp *.h -o gxde-lsg-mips64el --static
loongarch64-linux-gnu-g++ *.cpp *.h -o gxde-lsg-loong64 --static
riscv64-linux-gnu-g++ *.cpp *.h -o gxde-lsg-riscv64 --static
