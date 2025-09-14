# SVN LFS Export

## Dependencies
 - apr
 - svn
 - OpenSSL
 - libgit2

All other dependencies will be downloaded during configuration if required.

## Build
Configure from the root directory. Available presets `ninja` or `xcode`
```
cmake --preset=ninja
```

Build. Available configs are `Debug` `Release` `RelWithDebInfo` `MinSizeRel` and `Sanitize`
```
cmake --build build --config=release
```

Test
```
cmake --build build && ctest --test-dir build
```