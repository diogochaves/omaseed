import QtQuick
import QtTest
import "../../plugin/IpcOwner.js" as IpcOwner

// Who answers `omarchy-shell chaves.omaseed …`: the first live instance of the widget, and the
// next one once it goes, wherever each is hosted (the shell's bar, the console's rail). On
// 2026-10-09 an instance the rail hosted never got the target ("Target not found").
TestCase {
    name: "PluginIpcOwner"

    Component {
        id: fakeWidget

        QtObject {
            property string moduleName: "chaves.omaseed"
            property var elected: []

            function ipcElected(first) {
                elected = elected.concat([first]);
            }
        }
    }

    function last(widget) {
        return widget.elected[widget.elected.length - 1];
    }

    function cleanup() {
        IpcOwner.queue = [];
    }

    function test_firstToJoinOwns() {
        const bar = createTemporaryObject(fakeWidget, this);
        const rail = createTemporaryObject(fakeWidget, this);
        IpcOwner.join(bar);
        IpcOwner.join(rail);
        compare(last(bar), true);
        compare(last(rail), false);
        compare(IpcOwner.first(), bar);
    }

    function test_nextTakesOverWhenTheOwnerLeaves() {
        // The rail makes its instance before the bar's goes: the bar's owns, then hands over.
        const bar = createTemporaryObject(fakeWidget, this);
        const rail = createTemporaryObject(fakeWidget, this);
        IpcOwner.join(bar);
        IpcOwner.join(rail);
        IpcOwner.leave(bar);
        compare(last(rail), true);
        compare(IpcOwner.first(), rail);
    }

    function test_aLeavingFollowerChangesNothing() {
        const bar = createTemporaryObject(fakeWidget, this);
        const rail = createTemporaryObject(fakeWidget, this);
        IpcOwner.join(bar);
        IpcOwner.join(rail);
        IpcOwner.leave(rail);
        compare(last(bar), true);
        compare(IpcOwner.first(), bar);
    }

    function test_joiningTwiceQueuesOnce() {
        const bar = createTemporaryObject(fakeWidget, this);
        IpcOwner.join(bar);
        IpcOwner.join(bar);
        compare(IpcOwner.queue.length, 1);
    }

    function test_aDestroyedInstanceIsSkipped() {
        // Gone without leave() (a plugin reload): the next live one owns.
        const gone = fakeWidget.createObject(this);
        const rail = createTemporaryObject(fakeWidget, this);
        IpcOwner.join(gone);
        IpcOwner.join(rail);
        gone.destroy();
        wait(0);
        compare(IpcOwner.first(), rail);
    }

    function test_emptyQueueHasNoOwner() {
        compare(IpcOwner.first(), null);
    }
}
