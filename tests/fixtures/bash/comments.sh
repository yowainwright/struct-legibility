main() {
    # helper
    printf '%s\n' 'helper() { broken';
}
helper() { :; }
