import QtQuick
import QtTest
import Omaseed
import Omaseed.Kit
import Omaseed.Views

// Board 44 §4 S1–S3 (DECISIONS C17 D8–D9, #120): the add sheet's driver line. It shows only
// where the save disk's driver allocates every ticked file in full, names it, counts the
// ticked files' full size against the room, and names files FAT32 can't hold.
TestCase {
    id: testCase

    height: 400
    name: "AddDriverLine"
    visible: true
    when: windowShown
    width: 900

    readonly property real gb: 1000 * 1000 * 1000

    Component {
        id: lineComponent

        AddDriverLine {
            width: 860
        }
    }

    function ntfs3(free: real): var {
        return {
            "source": "local",
            "bytesFree": free,
            "fileSystem": "ntfs3",
            "allocatesInFull": true,
            "fat32": false
        };
    }

    function test_s1_names_the_driver_and_says_it_fits() {
        const line = createTemporaryObject(lineComponent, testCase, {
                                               "room": ntfs3(23 * testCase.gb),
                                               "neededBytes": 8 * testCase.gb
                                           });
        verify(line.visible);
        compare(findChild(line, "addDriverName").text, "[ntfs3]");
        verify(findChild(line, "addDriverLine").text.indexOf("so it fits") > 0);
        verify(!findChild(line, "addFat32Line").visible);
    }

    function test_s2_counts_the_full_size_and_says_what_to_do() {
        const line = createTemporaryObject(lineComponent, testCase, {
                                               "room": ntfs3(23 * testCase.gb),
                                               "neededBytes": 85.8 * testCase.gb
                                           });
        const text = findChild(line, "addDriverLine");
        verify(text.text.indexOf("Select fewer files, or save to another disk") > 0, text.text);
        compare(text.color, Theme.red);
    }

    function test_other_drivers_get_no_line() {
        for (const fs of ["ntfs", "ext4", "btrfs", "fuseblk", ""]) {
            const line = createTemporaryObject(lineComponent, testCase, {
                                                   "room": {
                                                       "source": "local",
                                                       "bytesFree": 1e12,
                                                       "fileSystem": fs,
                                                       "allocatesInFull": false
                                                   },
                                                   "neededBytes": 1e9
                                               });
            verify(!line.visible, fs);
        }
        // A server that is down says nothing of its disk.
        const down = createTemporaryObject(lineComponent, testCase, {
                                               "room": ntfs3(23 * testCase.gb),
                                               "neededBytes": 1e9,
                                               "serverDown": true
                                           });
        verify(!down.visible);
    }

    function test_s3_names_files_fat32_cannot_hold() {
        const room = ntfs3(30 * testCase.gb);
        room.fileSystem = "vfat";
        room.fat32 = true;
        const line = createTemporaryObject(lineComponent, testCase, {
                                               "room": room,
                                               "neededBytes": 12 * testCase.gb,
                                               "tooBig": [
                                                   {
                                                       "path": "films/a.mkv",
                                                       "size": 5e9
                                                   },
                                                   {
                                                       "path": "films/b.mkv",
                                                       "size": 6e9
                                                   }
                                               ]
                                           });
        const fat = findChild(line, "addFat32Line");
        verify(fat.visible);
        verify(fat.text.indexOf("2 files are 4 GiB or more") === 0, fat.text);
        verify(fat.text.indexOf("films/a.mkv") > 0);
        compare(findChild(line, "addFat32Skip").text, "Skip these 2");
    }
}
