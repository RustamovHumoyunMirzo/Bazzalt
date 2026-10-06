# Start at the linker-name symlink so all versioned runtime names are preserved.
file(COPY "${INPUT}" DESTINATION "${DESTINATION}" FOLLOW_SYMLINK_CHAIN)
