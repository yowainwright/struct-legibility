package example

func helper() {}
func run() {
	callback := func() { helper() }
	_ = callback
}
