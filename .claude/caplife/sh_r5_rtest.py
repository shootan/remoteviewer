p = 'C:/work/gnlink-wt/apk-prelogin-select/apps/android_direct_client/app/src/test/java/com/remote60/androiddirect/ViewerTouchRouterTest.kt'
s = open(p, 'rb').read().decode('utf-8')
nl = '\r\n' if '\r\n' in s else '\n'
s = s.replace('\r\n', '\n')
old = '''        assertEquals(ViewerTouchRouter.Owner.PC, r.route(ACTION_DOWN, localNow = false))
    }
}
'''
new = '''        assertEquals(ViewerTouchRouter.Owner.PC, r.route(ACTION_DOWN, localNow = false))
    }

    // sh-strength r5 (Codex codex_sh_strength_r4.md N1-c2): a stream begun while the new layout is not shown is
    // swallowed WHOLE -- a second finger, moves and a partial up never start sending, not even once the layout is shown
    // mid-stream; the next new DOWN (shown) is the PC's again. (r4 checked the first DOWN only: the POINTER_DOWN went
    // through the PC route with no active pointer and pressed.)
    @Test
    fun aStreamBegunBeforeTheLayoutIsShown_isSwallowedToItsEnd() {
        val r = ViewerTouchRouter()
        val swallow = ViewerTouchRouter.Owner.SWALLOW
        assertEquals(swallow, r.route(ACTION_DOWN, localNow = false, layoutShown = false))
        assertEquals(swallow, r.route(ACTION_POINTER_DOWN, localNow = false, layoutShown = false))
        assertEquals(swallow, r.route(ACTION_MOVE, localNow = false, layoutShown = false))
        assertEquals(swallow, r.route(ACTION_POINTER_UP, localNow = false, layoutShown = true))  // shown now
        assertEquals(swallow, r.route(ACTION_MOVE, localNow = false, layoutShown = true))
        assertEquals(swallow, r.route(ACTION_UP, localNow = false, layoutShown = true))
        assertEquals(ViewerTouchRouter.Owner.PC, r.route(ACTION_DOWN, localNow = false, layoutShown = true))
        assertEquals(ViewerTouchRouter.Owner.PC, r.route(ACTION_UP, localNow = false, layoutShown = true))
        // a cancel ends it the same way
        assertEquals(swallow, r.route(ACTION_DOWN, localNow = false, layoutShown = false))
        assertEquals(swallow, r.route(ACTION_CANCEL, localNow = false, layoutShown = false))
        assertEquals(ViewerTouchRouter.Owner.PC, r.route(ACTION_DOWN, localNow = false, layoutShown = true))
    }

    // 화면 이동 stays local whatever the layout: it sends nothing anyway
    @Test
    fun aLocalStreamIsLocalEvenBeforeTheLayoutIsShown() {
        val r = ViewerTouchRouter()
        assertEquals(ViewerTouchRouter.Owner.LOCAL, r.route(ACTION_DOWN, localNow = true, layoutShown = false))
        assertEquals(ViewerTouchRouter.Owner.LOCAL, r.route(ACTION_POINTER_DOWN, localNow = false, layoutShown = false))
    }

    // a PC stream under way when the layout changes: interrupted once (the caller releases), the rest swallowed
    @Test
    fun aPcStreamCutByALayoutChange_releasesOnce() {
        val r = ViewerTouchRouter()
        assertEquals(ViewerTouchRouter.Owner.PC, r.route(ACTION_DOWN, localNow = false, layoutShown = true))
        assertTrue(r.interrupt())
        assertFalse(r.interrupt())
        assertEquals(ViewerTouchRouter.Owner.SWALLOW, r.route(ACTION_POINTER_DOWN, localNow = false))
        assertEquals(ViewerTouchRouter.Owner.SWALLOW, r.route(ACTION_UP, localNow = false))
    }
}
'''
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
open(p, 'wb').write(s.replace('\n', nl).encode('utf-8'))
print('ok')
