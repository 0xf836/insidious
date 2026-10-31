# Shared by the container runner and its negative regression tests.
reject_matches() {
    local status
    if grep -Ei -- "$1" "$2"; then
        printf 'Forbidden diagnostic in %s\n' "$2" >&2
        return 1
    else
        status=$?
        if [[ "$status" -ne 1 ]]; then
            printf 'Cannot inspect diagnostics in %s\n' "$2" >&2
            return 1
        fi
    fi
}

# Preserve copy failures and require the source package alongside binary RPMs.
copy_rpms() {
    local sources
    find "$1/RPMS" "$1/SRPMS" -name '*.rpm' -exec cp -t "$2" -- {} + || return 1
    sources=("$2"/*.src.rpm)
    [[ "${#sources[@]}" == 1 && -f "${sources[0]}" ]]
}
