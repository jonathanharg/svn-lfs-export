# SVN LFS Export

A tool for incremental conversion of svn repositories to git with git LFS support

Accomplished by using [git fast-import](https://git-scm.com/docs/git-fast-import) and by writing LFS pointer directly to fast-import, as described by [this article](https://www.codethink.co.uk/articles/2023/gitlfsfastimport/).

## Features
 - **Incremental conversion** continue where the tool left off
 - **Git LFS support** track binary files based on configurable patterns
 - **Flexible branch and path mapping** use regex based rules to remap or exclude paths
 - **Translate commit metadata** convert usernames and preserve accurate commit timezones
 - **Fast, offline conversion** doesn't rely on an active server connection
 - **Highly configurable** tailor the conversion to your repositories needs

## Workflow

 1. Use `svnsync` to get an offline copy of your repository ([documentation](https://svnbook.red-bean.com/en/1.7/svn.ref.svnsync.html))

```bash
# Create a local svn repository with svnadmin, allow pre-revprop-change, then run
svnsync init file:///home/svn-mirror https://svn.example.com
svnsync sync file:///home/svn-mirror
```

 2. Write a `config.toml`
```bash
# Copy the example config and modify as required
svn-lfs-export --example-config > config.toml
```

 3. Run `svn-lfs-export`
 4. Once the conversion is complete, run `git gc --aggressive` to repack your repository (this may take some time)
 5. *(Optional)* re-run `svnsync sync` then `svn-lfs-export` to update with the latest change from svn.

This tool is best used as a one off conversion, or as a git mirror of your svn repository that you can use to incrementally migrate your tools and build systems to git.

## Install

Download precompiled [releases](https://github.com/jonathanharg/svn-lfs-export/releases) or [build from source](#build-from-source).

On Linux make sure `libsvn1` (or similar) is installed.

## FAQ

**How do I write a rule?**

Write regex to match svn paths and rewrite them to git branches and paths. Anything not captured by `svn_path` will be appended to `git_path`. See `example_config.toml` for examples.

**Why do I need to specify a branch origin point?**

Branches in svn can have mixed revisions, not all of a branch has to come from the same revision. Since svn-lfs-export replaces the whole contents of a branch, it doesn't know or care where a git branch originated from. Theoretically it could detect a sensible branch origin, but at the moment this needs to be explicitly provided (although improvements are welcome).

**What's an ambiguous svn revision?**

In some cases, a single svn revision can map to multiple git commits. For example a single revision can modify both `/trunk/foo.txt` and `/branches/my-branch/bar.txt`. These svn revisions **cannot** be used as a branching point in git, as they refer to multiple commits.

**What about tags?**

Svn tags should be treated as branches. These can then be replaced with git tags if needed once the conversion is done.

**What isn't supported?**

There are some features of svn that git doesn't have an equivalent of. Externals, file/directory properties and revision properties are all ignored by a conversion. However, symlink and executable file types are converted.

**Can I commit changes made to the git repository back to subversion?**

No. svn-lfs-export repositories aren't backwards compatible with svn or git-svn.

## Build from source

> Make sure you have vcpkg installed and configured correctly

On MacOS, make sure you have `svn` and `apr` installed with brew.

On Linux make sure `libsvn-dev` or similar is installed.

Configure CMake from the root directory. Available presets `ninja` or `xcode`
```
cmake --preset=ninja
```

Build, available configs are `Debug` `Release` `RelWithDebInfo` `MinSizeRel` and `Sanitize`
```
cmake --build build --config=Release
```
