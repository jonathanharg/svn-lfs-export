# SVN LFS Export

## Dependencies
 - apr
 - svn

Use vcpkg to install all other dependencies.

## Build
Configure from the root directory. Available presets `ninja` or `xcode`
```
cmake --preset=ninja
```

Build. Available configs are `Debug` `Release` `RelWithDebInfo` `MinSizeRel` and `Sanitize`
```
cmake --build build --config=Release
```
