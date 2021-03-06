// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chromoting.help;

import android.app.Activity;

/**
 * This class implements a basic UI for help-and-feedback.
 */
public class HelpAndFeedbackBasic implements HelpAndFeedback {
    @Override
    public void launchHelp(Activity activity, HelpContext helpContext) {
        HelpActivity.launch(activity, urlFromHelpContext(helpContext));
    }

    private static String urlFromHelpContext(HelpContext helpContext) {
        switch (helpContext) {
            case HOST_LIST:
                return "https://support.google.com/chrome/answer/6002441#hosts";
            case HOST_SETUP:
                return "https://support.google.com/chrome/answer/1649523";
            case DESKTOP:
                return "https://support.google.com/chrome/answer/6002441#gestures";
            default:
                // Unreachable, but required by Java style.
                assert false;
                return "";
        }
    }
}
