package example

// struct-lint-disable-next function-order -- fixture
func helper() int { return 1 }
func main() int   { return helper() }
