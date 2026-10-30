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
