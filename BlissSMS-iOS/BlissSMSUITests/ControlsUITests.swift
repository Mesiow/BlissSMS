import XCTest

/// Presses the on-screen pad like a finger would and checks the emulated joypad sees it.
/// Needs at least one ROM in the library (the first one is opened).
final class ControlsUITests: XCTestCase {

    private func launchIntoGame() -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments += ["-BlissOpenFirstRom", "-BlissUITest"]
        app.launch()
        XCTAssertTrue(app.staticTexts["inputDebug"].waitForExistence(timeout: 10), "game screen didn't open")
        return app
    }

    private func latch(_ app: XCUIApplication) -> String {
        let el = app.staticTexts["inputDebug"]
        print("DEBUGSTATE \(el.label) \(el.value ?? "")")
        return el.label
    }

    func testButtonOneReachesJoypad() {
        let app = launchIntoGame()
        app.descendants(matching: .any)["button1"].firstMatch.press(forDuration: 0.5)
        // BIT_A = $10
        XCTAssertEqual(latch(app), "touch=10 port=10")
    }

    func testButtonTwoReachesJoypad() {
        let app = launchIntoGame()
        app.descendants(matching: .any)["button2"].firstMatch.press(forDuration: 0.5)
        // BIT_B = $20
        XCTAssertEqual(latch(app), "touch=20 port=20")
    }

    func testDPadRightReachesJoypad() {
        let app = launchIntoGame()
        let pad = app.descendants(matching: .any)["dpad"].firstMatch
        pad.coordinate(withNormalizedOffset: CGVector(dx: 0.9, dy: 0.5)).press(forDuration: 0.5)
        // BIT_RIGHT = $08
        XCTAssertEqual(latch(app), "touch=08 port=08")
    }

    /// Same as a real user: tap the game in the library instead of the debug shortcut.
    func testButtonWorksWhenOpenedFromLibrary() {
        let app = XCUIApplication()
        // Churn: the library refreshes every 0.5 s while the game runs (this used to break input)
        app.launchArguments += ["-BlissUITest", "-BlissUITestChurn"]
        app.launch()
        let card = app.staticTexts["Sunset Scroller"].firstMatch
        XCTAssertTrue(card.waitForExistence(timeout: 10), "library didn't show the test ROM")
        card.tap()
        XCTAssertTrue(app.staticTexts["inputDebug"].waitForExistence(timeout: 10), "game screen didn't open")
        sleep(2)
        app.descendants(matching: .any)["button1"].firstMatch.press(forDuration: 0.5)
        sleep(2)
        app.descendants(matching: .any)["button2"].firstMatch.press(forDuration: 0.5)
        XCTAssertEqual(latch(app), "touch=30 port=30")
    }

    /// Pause freezes the game and the button switches to RESUME. Also saves screenshots of
    /// portrait + landscape when BLISS_SCREENSHOT_DIR is set (for checking the layout).
    func testPauseShowsResumeAndLandscapeLayout() throws {
        let app = launchIntoGame()
        sleep(2)
        app.descendants(matching: .any)["Pause"].firstMatch.press(forDuration: 0.1)
        XCTAssertTrue(app.descendants(matching: .any)["Resume"].firstMatch.waitForExistence(timeout: 6))
        save(app, "portrait-paused")

        app.descendants(matching: .any)["Resume"].firstMatch.press(forDuration: 0.1)
        XCTAssertTrue(app.descendants(matching: .any)["Pause"].firstMatch.waitForExistence(timeout: 6))

        XCUIDevice.shared.orientation = .landscapeLeft
        sleep(2)
        save(app, "landscape")
        XCUIDevice.shared.orientation = .portrait
    }

    private func save(_ app: XCUIApplication, _ name: String) {
        let shot = XCUIScreen.main.screenshot()
        let attachment = XCTAttachment(screenshot: shot)
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
        if let dir = ProcessInfo.processInfo.environment["BLISS_SCREENSHOT_DIR"] {
            try? shot.pngRepresentation.write(to: URL(fileURLWithPath: dir).appendingPathComponent("\(name).png"))
        }
    }

    func testQuickSaveLoadAndSlots() {
        let app = launchIntoGame()
        sleep(2)
        let menu = app.descendants(matching: .any)["gameMenu"].firstMatch

        menu.tap()
        app.buttons["Quick Save"].tap()

        // The "Saved" toast is brief; check something that lasts: Quick Load is now enabled
        sleep(2)
        menu.tap()
        XCTAssertTrue(app.buttons["Quick Load"].waitForExistence(timeout: 6))
        XCTAssertTrue(app.buttons["Quick Load"].isEnabled)
        app.buttons["Quick Load"].tap()

        sleep(2)
        menu.tap()
        app.buttons["Save States…"].tap()
        XCTAssertTrue(app.navigationBars["Save States"].waitForExistence(timeout: 6))
        XCTAssertTrue(app.staticTexts["Slot 1"].exists)
        app.buttons.matching(identifier: "Save").element(boundBy: 1).tap()   // row 0 = quick, row 1 = slot 1
        // Slot 1 may already be full from an earlier run
        if app.buttons["Overwrite"].waitForExistence(timeout: 1) { app.buttons["Overwrite"].tap() }
        sleep(1)
        save(app, "save-states")
        app.buttons.matching(identifier: "Load").element(boundBy: 1).tap()
        XCTAssertTrue(app.staticTexts["Loaded Slot 1"].waitForExistence(timeout: 6))
        save(app, "after-load")
    }
}
