# stage_assets.cmake: run via `cmake -DANO_ASSETS_SRC=<dir> -DANO_ASSETS_DST=<dir> -P`.
# Copies assets/ into a build tree without its version control metadata.
#
# `cmake -E copy_directory` has no exclude filter, and assets/ is a full git clone when the
# private pack is in use (the public assets-free pack is staged from the Nix store and has
# none). Copying its .git plants a second repository inside the build tree, where any git
# command run from a build directory silently targets the assets repo instead of the engine.

if(NOT ANO_ASSETS_SRC OR NOT ANO_ASSETS_DST)
    message(FATAL_ERROR "stage_assets: ANO_ASSETS_SRC and ANO_ASSETS_DST required")
endif()

# Trailing slash on the source copies its *contents*, matching copy_directory's shape.
file(COPY "${ANO_ASSETS_SRC}/" DESTINATION "${ANO_ASSETS_DST}" PATTERN ".git" EXCLUDE)
