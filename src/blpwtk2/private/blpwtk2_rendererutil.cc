/*
 * Copyright (C) 2015 Bloomberg Finance L.P.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS," WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <blpwtk2_rendererutil.h>
#include <blpwtk2_blob.h>
#include <blpwtk2_statics.h>

#include <base/logging.h>  // for DCHECK

#include <content/renderer/render_widget.h>
#include <content/browser/renderer_host/web_input_event_aura.h>
#include <content/public/browser/native_web_keyboard_event.h>
#include <third_party/WebKit/public/web/WebInputEvent.h>
#include <ui/events/event.h>

 #include <content/public/renderer/render_view.h>
#include <third_party/WebKit/public/web/WebView.h>
#include <third_party/WebKit/public/web/WebFrame.h>
#include <skia/ext/platform_canvas.h>
#include <third_party/skia/include/core/SkDocument.h>
#include <third_party/skia/include/core/SkStream.h>
#include <pdf/pdf.h>
#include <ui/gfx/geometry/size.h>

namespace blpwtk2 {

void RendererUtil::handleInputEvents(content::RenderWidget *rw, const WebView::InputEvent *events, size_t eventsCount)
{
    for (size_t i=0; i < eventsCount; ++i) {
        const WebView::InputEvent *event = events + i;
        MSG msg = {
            event->hwnd,
            event->message,
            event->wparam,
            event->lparam,
            GetMessageTime()
        };

        switch (event->message) {
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYUP:
        case WM_IME_CHAR:
        case WM_SYSCHAR:
        case WM_CHAR: {
            ui::KeyEvent uiKeyboardEvent(msg);
            content::NativeWebKeyboardEvent blinkKeyboardEvent(&uiKeyboardEvent);

            blinkKeyboardEvent.modifiers &= ~(
                    blink::WebInputEvent::ShiftKey |
                    blink::WebInputEvent::ControlKey |
                    blink::WebInputEvent::AltKey |
                    blink::WebInputEvent::MetaKey |
                    blink::WebInputEvent::IsAutoRepeat |
                    blink::WebInputEvent::IsKeyPad |
                    blink::WebInputEvent::IsLeft |
                    blink::WebInputEvent::IsRight |
                    blink::WebInputEvent::NumLockOn |
                    blink::WebInputEvent::CapsLockOn
                );

            if (event->shiftKey)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::ShiftKey;

            if (event->controlKey)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::ControlKey;

            if (event->altKey)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::AltKey;

            if (event->metaKey)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::MetaKey;

            if (event->isAutoRepeat)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::IsAutoRepeat;

            if (event->isKeyPad)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::IsKeyPad;

            if (event->isLeft)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::IsLeft;

            if (event->isRight)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::IsRight;

            if (event->numLockOn)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::NumLockOn;

            if (event->capsLockOn)
                blinkKeyboardEvent.modifiers |= blink::WebInputEvent::CapsLockOn;

            rw->bbHandleInputEvent(blinkKeyboardEvent);
        } break;

        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP: {
            ui::MouseEvent uiMouseEvent(msg);
            blink::WebMouseEvent blinkMouseEvent = content::MakeWebMouseEvent(uiMouseEvent);
            rw->bbHandleInputEvent(blinkMouseEvent);
        } break;

        case WM_MOUSEWHEEL: {
            ui::MouseWheelEvent uiMouseWheelEvent(msg);
            blink::WebMouseWheelEvent blinkMouseWheelEvent = content::MakeWebMouseWheelEvent(uiMouseWheelEvent);
            rw->bbHandleInputEvent(blinkMouseWheelEvent);
        } break;
        }
    }
}

void RendererUtil::drawContentsToBlob(content::RenderView        *rv,
                                      Blob                       *blob,
                                      const WebView::DrawParams&  params)
{
    blink::WebFrame* webFrame = rv->GetWebView()->mainFrame();
    DCHECK(webFrame->isWebLocalFrame());

    int srcWidth = params.srcRegion.right - params.srcRegion.left;
    int srcHeight = params.srcRegion.bottom - params.srcRegion.top;

    if (params.rendererType == WebView::DrawParams::RendererTypePDF) {
        SkDynamicMemoryWStream& pdf_stream = blob->makeSkStream();
        {
            skia::RefPtr<SkDocument> document = skia::AdoptRef(SkDocument::CreatePDF(&pdf_stream, params.dpi));
            SkCanvas *canvas = document->beginPage(params.destWidth, params.destHeight);
            canvas->scale(params.destWidth / srcWidth, params.destHeight / srcHeight);

            webFrame->drawInCanvas(blink::WebRect(params.srcRegion.left, params.srcRegion.top, srcWidth, srcHeight),
                                   blink::WebString::fromUTF8(params.styleClass.data(), params.styleClass.length()),
                                   canvas);
            canvas->flush();
            document->endPage();
        }
    }
    else if (params.rendererType == WebView::DrawParams::RendererTypeBitmap) {
        SkBitmap& bitmap = blob->makeSkBitmap();
        bitmap.allocN32Pixels(params.destWidth + 0.5, params.destHeight + 0.5);

        SkCanvas canvas(bitmap);
        canvas.scale(params.destWidth / srcWidth, params.destHeight / srcHeight);

        webFrame->drawInCanvas(blink::WebRect(params.srcRegion.left, params.srcRegion.top, srcWidth, srcHeight),
                               blink::WebString::fromUTF8(params.styleClass.data(), params.styleClass.length()),
                               &canvas);

        canvas.flush();
    }
}

void RendererUtil::setLCDTextShouldBlendWithCSSBackgroundColor(int renderViewRoutingId,
                                                               bool enable)
{
    DCHECK(Statics::isInApplicationMainThread());

    content::RenderView* rv = content::RenderView::FromRoutingID(renderViewRoutingId);
    rv->GetWebView()->setLCDTextShouldBlendWithCSSBackgroundColor(enable);
}

}  // close namespace blpwtk2
