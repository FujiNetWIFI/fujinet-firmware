# build.sh documentation

By default (i.e. without any command line args), build.sh requires an ini file
named `platformio-generated.ini` to build.

This file is created every time build.sh is run from the following files:

- platformio.common.ini
- build-platforms/platformio-{build_board}.ini
- platformio.local.ini

This allows users to keep just their changes in the git-ignored file `platformio.local.ini`, but receive upstream changes (like platformio version updates etc) when they build.

## Generating a new local file

To generate a new local file, run the following:

```sh
# use fujinet-atari-v1 for example
./build.sh -s fujinet-atari-v1
```

Board names can be found in `build-platforms/platformio-*.ini`

This will overwrite the default file `platformio.local.ini` so use wisely.

You can specify the local file that will be used (in both reading and generating as new) with the `-l FILE` parameter

```sh
./build.sh -s fujinet-atari-v1 -l platformio.mylocal-atari.ini
```

## Format of local file

This is a standard ini file with one additional feature; you can specify `+=` to add to existing values, rather than fully overwriting them with local values.

```ini
[fujinet]
build_board = fujinet-atari-v1

[env]
upload_port = /dev/ttyOTHER
build_flags +=
        -D CORE_DEBUG_LEVEL=5
        -D FNCONFIG_DEBUG=1
        -D VERBOSE_HTTP

[env:fujinet-atari-v1]

build_flags +=
        -D FN_HISPEED_INDEX=0
```

Using the `-n BUILD_BOARD` option detailed previously will generate a new `platformio.local.ini` file with just the `[fujinet]` section and the named BUILD_BOARD value set.
This is the only value that MUST exist in the local file, else the build will exit with an error, as it will not know which template to use for loading board specific values from.

In the above example, `+=` was used to add values to the common `build_flags` values in other sections.

Values assigned with just `=`, will override any common or board specific values, as in the example of `env.upload_port`.

## generating specific ini files

The default is setup so users don't have to specify any arguments to build.

However, if you want to generate different ini files, you can do this directly with the `-i INI_FILE_NAME` and `-l LOCAL_INI_VALUES` parameters

```sh
./build.sh -i platformio-my-full-generated-file.ini -l platformio.local-atari-release.ini
```

As the build always re-generates the INI file used for a build, this will create the file `platformio-my-full-generated-file.ini` but not run a build, as no build args were specified.

Note: Absolute and relative paths can be specified for names of files and do not have to be in the current working directory.

### Example - running a release/zip build

If you want to create a ZIP release, you can use a local ini file called `platformio.local-atari-release.ini` as follows:

```ini
; ... normal changes
[env]
extra_scripts +=
    post:build_firmwarezip.py
```

and then generate a build with

```sh
./build.sh -z -i platformio-release.ini -l platformio.local-atari-release.ini
```

This will first create a full generated platformio ini file named `platformio-release.ini`, using the local changes in `platformio.local-atari-release.ini`

Git ignores all files of form `platformio.local*.ini` and `platformio-release.ini` so they won't be checked into the repository.
