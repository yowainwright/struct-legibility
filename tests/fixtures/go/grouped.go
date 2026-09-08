package example

import (
	"fmt"
	"strings"
)

type (
	Count int
	Label string
)

const (
	prefix = "value"
	limit  = 1
)

var (
	count Count
	label Label
)

func main() { fmt.Println(strings.TrimSpace(prefix), limit, count, label) }
