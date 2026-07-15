$ErrorActionPreference = "Continue"
$BUILDDIR = "build/obj"
New-Item -ItemType Directory -Force -Path $BUILDDIR | Out-Null

$CFLAGS = "-std=gnu11 -pipe -O2 -funroll-loops -fomit-frame-pointer -ffunction-sections -fdata-sections -fstrict-aliasing -g0 -DNDEBUG -fno-exceptions -Wimplicit-function-declaration -D_GNU_SOURCE"
$SYSCFLAGS = "-DLUA_DL_DLOPEN -DLUA_COMPAT_MATHLIB -DLUA_COMPAT_MAXN -DLUA_COMPAT_MODULE"
$INC = "-Isrc/core -Isrc/stdlib -Isrc/vm -Isrc/compiler -Isrc/utils -Isrc/wasm -Isrc/bin -Iquickjs -Isrc/lua2wasm -Ipcre2 -DPCRE2_CODE_UNIT_WIDTH=8 -DHAVE_CONFIG_H -Iwasmtime/wasmtime-v45.0.1-x86_64-mingw-c-api/include"
$ALLCFLAGS = "$CFLAGS $SYSCFLAGS $INC"

Write-Host "=== Compiling last.c ==="
gcc $ALLCFLAGS.Split(" ") -c src/compiler/last.c -o "$BUILDDIR/last.o" 2>&1
Write-Host "last.o exit: $LASTEXITCODE"

Write-Host "=== Compiling last_parse.c ==="
gcc $ALLCFLAGS.Split(" ") -c src/compiler/last_parse.c -o "$BUILDDIR/last_parse.o" 2>&1
Write-Host "last_parse.o exit: $LASTEXITCODE"

Write-Host "=== Compiling last_visitor.c ==="
gcc $ALLCFLAGS.Split(" ") -c src/compiler/last_visitor.c -o "$BUILDDIR/last_visitor.o" 2>&1
Write-Host "last_visitor.o exit: $LASTEXITCODE"

Write-Host "=== Compiling lcodegen.c ==="
gcc $ALLCFLAGS.Split(" ") -c src/compiler/lcodegen.c -o "$BUILDDIR/lcodegen.o" 2>&1
Write-Host "lcodegen.o exit: $LASTEXITCODE"

Write-Host "=== Compiling lparser.c ==="
gcc $ALLCFLAGS.Split(" ") -c src/compiler/lparser.c -o "$BUILDDIR/lparser.o" 2>&1
Write-Host "lparser.o exit: $LASTEXITCODE"

Write-Host "=== All done ==="
