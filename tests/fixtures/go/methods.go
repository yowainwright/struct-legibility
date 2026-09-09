package example

type One struct{}
type Two struct{}

func (One) helper() {}
func (Two) helper() {}
func main()         { helper() }
func helper()       {}
