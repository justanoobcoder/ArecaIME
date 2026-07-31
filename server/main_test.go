package main

import (
	"context"
	"testing"
)

type countingBackspaceSender struct {
	count int
}

func (sender *countingBackspaceSender) backspace() error {
	sender.count++
	return nil
}

func TestRunPlanSendsDeleteCountPlusSentinelAndOneDone(t *testing.T) {
	sender := &countingBackspaceSender{}
	out := make(chan string, 2)
	plan := planCommand{sessionID: 41, txID: 7, backspaces: 3}

	runPlan(context.Background(), out, plan, sender)

	wantEvents := plan.backspaces + 1
	if sender.count != wantEvents {
		t.Fatalf("sent %d Backspaces, want %d including sentinel", sender.count, wantEvents)
	}
	select {
	case response := <-out:
		if response != "DONE 41 7\n" {
			t.Fatalf("response = %q, want DONE for the plan", response)
		}
	default:
		t.Fatal("server did not emit DONE")
	}
	select {
	case extra := <-out:
		t.Fatalf("server emitted an extra response: %q", extra)
	default:
	}
}
