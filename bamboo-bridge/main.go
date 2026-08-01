package main

/*
#include <stdint.h>
#include <stdlib.h>
*/
import "C"

import (
	"sort"
	"strings"
	"sync"

	bamboo "github.com/BambooEngine/bamboo-core"
)

var engines = struct {
	sync.Mutex
	next uint64
	byID map[uint64]bamboo.IEngine
}{next: 1, byID: make(map[uint64]bamboo.IEngine)}

//export ArecaBambooCreate
func ArecaBambooCreate(inputMethod *C.char, modernStyle C.int) C.uint64_t {
	name := "Telex 2"
	if inputMethod != nil && C.GoString(inputMethod) != "" {
		name = C.GoString(inputMethod)
	}
	method := bamboo.ParseInputMethod(bamboo.InputMethodDefinitions, name)
	if method.Name == "" {
		return 0
	}
	flags := uint(bamboo.EstdFlags)
	// ModernStyle selects oà/uý, while the Bamboo standard-tone flag selects
	// òa/úy.
	if modernStyle != 0 {
		flags &^= bamboo.EstdToneStyle
	}
	engine := bamboo.NewEngine(method, flags)
	engines.Lock()
	id := engines.next
	engines.next++
	engines.byID[id] = engine
	engines.Unlock()
	return C.uint64_t(id)
}

//export ArecaBambooDestroy
func ArecaBambooDestroy(id C.uint64_t) {
	engines.Lock()
	delete(engines.byID, uint64(id))
	engines.Unlock()
}

func engineFor(id C.uint64_t) bamboo.IEngine {
	engines.Lock()
	defer engines.Unlock()
	return engines.byID[uint64(id)]
}

//export ArecaBambooInputMethodNames
func ArecaBambooInputMethodNames() *C.char {
	preferred := map[string]int{
		"Telex": 0, "Telex 2": 1, "VNI": 2, "VIQR": 3,
		"Telex + VNI": 4, "Telex + VNI + VIQR": 5,
		"Microsoft layout": 6, "VNI Bàn phím tiếng Pháp": 7,
	}
	names := make([]string, 0, len(bamboo.InputMethodDefinitions))
	for name := range bamboo.InputMethodDefinitions {
		names = append(names, name)
	}
	sort.Slice(names, func(i, j int) bool {
		left, leftPreferred := preferred[names[i]]
		right, rightPreferred := preferred[names[j]]
		if leftPreferred != rightPreferred {
			return leftPreferred
		}
		if leftPreferred {
			return left < right
		}
		return names[i] < names[j]
	})
	return C.CString(strings.Join(names, "\n"))
}

//export ArecaBambooCharsetNames
func ArecaBambooCharsetNames() *C.char {
	names := bamboo.GetCharsetNames()
	sort.Slice(names[1:], func(i, j int) bool {
		return names[i+1] < names[j+1]
	})
	return C.CString(strings.Join(names, "\n"))
}

//export ArecaBambooEncode
func ArecaBambooEncode(charset *C.char, input *C.char) *C.char {
	if input == nil {
		return nil
	}
	charsetName := bamboo.UNICODE
	if charset != nil && C.GoString(charset) != "" {
		charsetName = C.GoString(charset)
	}
	return C.CString(bamboo.Encode(charsetName, C.GoString(input)))
}

//export ArecaBambooCanProcess
func ArecaBambooCanProcess(id C.uint64_t, key C.uint32_t) C.int {
	engine := engineFor(id)
	if engine != nil && engine.CanProcessKey(rune(key)) {
		return 1
	}
	return 0
}

//export ArecaBambooProcess
func ArecaBambooProcess(id C.uint64_t, key C.uint32_t) *C.char {
	engine := engineFor(id)
	if engine == nil {
		return nil
	}
	engine.ProcessKey(rune(key), bamboo.VietnameseMode)
	return C.CString(engine.GetProcessedString(bamboo.VietnameseMode))
}

//export ArecaBambooFinalizeWord
func ArecaBambooFinalizeWord(id C.uint64_t, spellCheck C.int) *C.char {
	engine := engineFor(id)
	if engine == nil {
		return nil
	}

	text := engine.GetProcessedString(bamboo.VietnameseMode)
	if spellCheck != 0 && bamboo.HasAnyVietnameseRune(text) && !engine.IsValid(true) {
		engine.RestoreLastWord(false)
		text = engine.GetProcessedString(bamboo.EnglishMode)
	}
	engine.Reset()
	return C.CString(text)
}

//export ArecaBambooBackspace
func ArecaBambooBackspace(id C.uint64_t) *C.char {
	engine := engineFor(id)
	if engine == nil {
		return nil
	}
	engine.RemoveLastChar(true)
	return C.CString(engine.GetProcessedString(bamboo.VietnameseMode))
}

//export ArecaBambooReset
func ArecaBambooReset(id C.uint64_t) {
	if engine := engineFor(id); engine != nil {
		engine.Reset()
	}
}

func main() {}
