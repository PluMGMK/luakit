--- Test that views keep reaching their page's web process.
--
-- @copyright 2026 Ícaro Andrade

local T = {}
local test = require "tests.lib"
local assert = require "luassert"

local uri = test.http_server() .. "scroll.html"

uris = { uri }
require "config.rc"

local settings = require "settings"
local window = require "window"
local w = assert(select(2, next(window.bywidget)))

local function eval_js(view, js)
    view:eval_js(js, { callback = test.continue })
    return test.wait(2000)
end

-- Waits until the current view shows the test page
local function wait_for_test_page()
    for _ = 1, 100 do
        if not w.view.is_loading and eval_js(w.view, "location.href") == uri then return end
        test.delay(50)
    end
    error("the test page didn't load")
end

-- eval_js and scrolling both go through the view's web process
local function assert_current_view_reaches_its_page()
    assert.is_equal(uri, eval_js(w.view, "location.href"), "eval_js got no answer")
    w:scroll{ y = 0 }
    test.wait_until(function () return w.view.scroll.y == 0 end)
    w:scroll{ yrel = 100 }
    assert.is_equal(100, eval_js(w.view, "window.scrollY"), "scrolling failed")
end

T.test_pop_up_outlives_its_opener = function ()
    test.wait_for_idle()
    wait_for_test_page()
    -- Settings apply to a view when a page loads
    settings.set_setting("webview.javascript_can_open_windows_automatically", true)
    w.view:reload()
    wait_for_test_page()
    local opener = w.view

    -- WebKit runs a pop-up in its opener's web process
    eval_js(opener, "window.open(location.href); true")
    test.wait_until(function () return w.tabs:count() == 2 end)
    local popup
    for _, view in ipairs(w.tabs.children) do
        if view ~= opener then popup = view end
    end
    test.wait_until(function () return popup.uri == uri and not popup.is_loading end)

    -- Destroys the opener, which releases its hold on the web process
    w:close_tab(opener)

    assert.is_equal(popup, w.view)
    assert_current_view_reaches_its_page()
end

T.test_view_reaches_its_page_after_a_crash = function ()
    test.wait_for_idle()
    wait_for_test_page()

    w.view:crash()
    test.wait_for_signal(w.view, "crashed", 5000)
    -- luakit shows its error page
    test.wait_for_view(w.view)

    -- Reloading, as the error page does, starts a new web process
    w.view:reload()
    wait_for_test_page()
    assert_current_view_reaches_its_page()
end

return T

-- vim: et:sw=4:ts=8:sts=4:tw=80
