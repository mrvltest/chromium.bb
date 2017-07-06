/*
 * Copyright (C) 2014 Bloomberg Finance L.P.
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

#include <blpwtk2_processhostimpl.h>

#include <blpwtk2_browsercontextimpl.h>
#include <blpwtk2_channelinfo.h>
#include <blpwtk2_constants.h>
#include <blpwtk2_control_messages.h>
#include <blpwtk2_desktopstreamsregistry.h>
#include <blpwtk2_managedrenderprocesshost.h>
#include <blpwtk2_processhostmanager.h>
#include <blpwtk2_products.h>
#include <blpwtk2_profile_messages.h>
#include <blpwtk2_profilehost.h>
#include <blpwtk2_rendererinfomap.h>
#include <blpwtk2_statics.h>
#include <blpwtk2_toolkit.h>
#include <blpwtk2_webview_messages.h>
#include <blpwtk2_webviewhost.h>
#include <blpwtk2_webviewhostobserver.h>
#include <blpwtk2_utility.h>

#include <base/command_line.h>
#include <base/process/process_iterator.h>  // for kProcessAccess* constants
#include <base/process/process_handle.h>
#include <content/browser/gpu/compositor_util.h>
#include <content/public/browser/browser_thread.h>
#include <content/public/browser/render_process_host.h>
#include <ipc/ipc_channel_proxy.h>
#include <printing/backend/print_backend.h>

namespace blpwtk2 {

ProcessHostImpl::ProcessHostImpl(RendererInfoMap* rendererInfoMap)
: d_processHandle(base::kNullProcessHandle)
, d_rendererInfoMap(rendererInfoMap)
, d_lastRoutingId(0x10000)
, d_receivedFinalSync(false)
{
    DCHECK(d_rendererInfoMap);

    scoped_refptr<base::SingleThreadTaskRunner> ioTaskRunner =
        content::BrowserThread::GetMessageLoopProxyForThread(
            content::BrowserThread::IO);

    std::string channelId = IPC::Channel::GenerateVerifiedChannelID(BLPWTK2_VERSION);
    d_channel = IPC::ChannelProxy::Create(channelId,
                                          IPC::Channel::MODE_SERVER,
                                          this,
                                          ioTaskRunner);
}

ProcessHostImpl::~ProcessHostImpl()
{
    // Load the listeners into a vector first, because their destructors will
    // remove themselves from the d_routes map.
    std::vector<ProcessHostListener*> listeners;
    listeners.reserve(d_routes.size());
    for (IDMap<ProcessHostListener>::Iterator<ProcessHostListener> it(&d_routes); !it.IsAtEnd(); it.Advance()) {
        listeners.push_back(it.GetCurrentValue());
    }
    for (size_t i = 0; i < listeners.size(); ++i) {
        delete listeners[i];
    }
    DCHECK(0 == d_routes.size());

    d_channel.reset();

    // This needs to use DeleteSoon because WebViewImpl::destroy uses
    // DeleteSoon, and we need to ensure that the render process host outlives
    // the WebViewImpl.
    // TODO(SHEZ): See if we can just delete the WebViewImpl directly
    //             instead of using DeleteSoon.
    base::MessageLoop::current()->DeleteSoon(
        FROM_HERE,
        d_renderProcessHost.release());

    if (d_processHandle != base::kNullProcessHandle) {
        ::CloseHandle(d_processHandle);
    }
}

const std::string& ProcessHostImpl::channelId() const
{
    return d_channel->ChannelId();
}

std::string ProcessHostImpl::channelInfo() const
{
    base::CommandLine commandLine(base::CommandLine::NO_PROGRAM);

    // TODO(SHEZ): We are missing kDisableDatabases for incognito profiles
    //             because we don't know yet which profile will be used for the
    //             in-process renderer.  We need to either have the profile
    //             specified upfront, or we need to handle kDisableDatabases
    //             once the profile is known.
    content::RenderProcessHost::AdjustCommandLineForRenderer(&commandLine);

    ChannelInfo channelInfo;
    channelInfo.d_channelId = channelId();
    channelInfo.loadSwitchesFromCommandLine(commandLine);
    return channelInfo.serialize();
}

// ProcessHost overrides

void ProcessHostImpl::addRoute(int routingId, ProcessHostListener* listener)
{
    LOG(INFO) << "Adding route: routingId(" << routingId << ")";
    d_routes.AddWithID(listener, routingId);
}

void ProcessHostImpl::removeRoute(int routingId)
{
    d_routes.Remove(routingId);
    LOG(INFO) << "Removed route: routingId(" << routingId << ")";
}

ProcessHostListener* ProcessHostImpl::findListener(int routingId)
{
    return d_routes.Lookup(routingId);
}

int ProcessHostImpl::getUniqueRoutingId()
{
    return ++d_lastRoutingId;
}

base::ProcessHandle ProcessHostImpl::processHandle()
{
    return d_processHandle;
}

// IPC::Sender overrides

bool ProcessHostImpl::Send(IPC::Message* message)
{
    return d_channel->Send(message);
}

// IPC::Listener overrides

bool ProcessHostImpl::OnMessageReceived(const IPC::Message& message)
{
    if (message.routing_id() == MSG_ROUTING_CONTROL) {
        // Dispatch control messages
        IPC_BEGIN_MESSAGE_MAP(ProcessHostImpl, message)
            IPC_MESSAGE_HANDLER(BlpControlHostMsg_Sync, onSync)
            IPC_MESSAGE_HANDLER(BlpControlHostMsg_CreateNewHostChannel, onCreateNewHostChannel)
            IPC_MESSAGE_HANDLER(BlpControlHostMsg_ClearWebCache, onClearWebCache)
            IPC_MESSAGE_HANDLER(BlpControlHostMsg_RegisterNativeViewForStreaming, onRegisterNativeViewForStreaming)
            IPC_MESSAGE_HANDLER(BlpProfileHostMsg_New, onProfileNew)
            IPC_MESSAGE_HANDLER(BlpProfileHostMsg_Destroy, onProfileDestroy)
            IPC_MESSAGE_HANDLER(BlpWebViewHostMsg_New, onWebViewNew)
            IPC_MESSAGE_HANDLER(BlpWebViewHostMsg_Destroy, onWebViewDestroy)
            IPC_MESSAGE_HANDLER(BlpControlHostMsg_DumpDiagnoticInfo, onDumpDiagnoticInfo)
            IPC_MESSAGE_HANDLER(BlpControlHostMsg_SetDefaultPrinterName, onSetDefaultPrinterName)
            IPC_MESSAGE_UNHANDLED_ERROR()
        IPC_END_MESSAGE_MAP()

        return true;
    }

    // Dispatch incoming messages to the appropriate IPC::Listener.
    IPC::Listener* listener = d_routes.Lookup(message.routing_id());
    if (!listener) {
        if (message.is_sync()) {
            // The listener has gone away, so we must respond or else the
            // caller will hang waiting for a reply.
            IPC::Message* reply = IPC::SyncMessage::GenerateReply(&message);
            reply->set_reply_error();
            Send(reply);
        }

        LOG(WARNING) << "message received, but no listener: routingId("
                     << message.routing_id() << ") type(" << message.type()
                     << ")";
        return true;
    }

    return listener->OnMessageReceived(message);
}

void ProcessHostImpl::OnBadMessageReceived(const IPC::Message& message)
{
    LOG(ERROR) << "bad message " << message.type();
}

void ProcessHostImpl::OnChannelConnected(int32_t peer_pid)
{
    LOG(INFO) << "channel connected: peer_pid(" << peer_pid << ")";
    if (peer_pid == (int32_t)base::GetCurrentProcId()) {
        d_processHandle = base::GetCurrentProcessHandle();
        CHECK(d_processHandle != base::kNullProcessHandle);
    }
    else {
        d_processHandle = ::OpenProcess(
            PROCESS_DUP_HANDLE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE,
            FALSE,
            peer_pid);
        CHECK(d_processHandle != base::kNullProcessHandle);
    }
}

void ProcessHostImpl::OnChannelError()
{
    if (!d_receivedFinalSync) {
        LOG(ERROR) << "channel error!";
        if (Statics::channelErrorHandler) {
            Statics::channelErrorHandler(1);
        }
    }
}

// Control message handlers

void ProcessHostImpl::onSync(bool isFinalSync)
{
    LOG(INFO) << "sync";
    if (isFinalSync)
        d_receivedFinalSync = true;
}

void ProcessHostImpl::onCreateNewHostChannel(int timeoutInMilliseconds,
                                             std::string* channelInfo)
{
    DCHECK(Statics::processHostManager);

    ProcessHostImpl* newProcessHost = new ProcessHostImpl(d_rendererInfoMap);
    *channelInfo = newProcessHost->channelInfo();
    Statics::processHostManager->addProcessHost(
        newProcessHost,
        base::TimeDelta::FromMilliseconds(timeoutInMilliseconds));
}

void ProcessHostImpl::onClearWebCache()
{
    content::RenderProcessHost::ClearWebCacheOnAllRenderers();
}

void ProcessHostImpl::onRegisterNativeViewForStreaming(NativeViewForTransit view,
                                                       std::string* result)
{
    *result = DesktopStreamsRegistry::RegisterNativeViewForStreaming((NativeView)view);
}

void ProcessHostImpl::onProfileNew(int routingId,
                                   const std::string& dataDir,
                                   bool diskCacheEnabled,
                                   bool cookiePersistenceEnabled)
{
    LOG(INFO) << "onProfileNew routingId(" << routingId << ")";
    new ProfileHost(this,
                    routingId,
                    dataDir,
                    diskCacheEnabled,
                    cookiePersistenceEnabled);
}

void ProcessHostImpl::onProfileDestroy(int routingId)
{
    LOG(INFO) << "onProfileDestroy routingId(" << routingId << ")";
    ProfileHost* profileHost =
        static_cast<ProfileHost*>(findListener(routingId));
    DCHECK(profileHost);
    delete profileHost;
}

void ProcessHostImpl::onWebViewNew(const BlpWebViewHostMsg_NewParams& params)
{
    LOG(INFO) << "onWebViewNew routingId(" << params.routingId << ")";
    ProfileHost* profileHost =
        static_cast<ProfileHost*>(findListener(params.profileId));
    DCHECK(profileHost);

    int hostAffinity;
    bool isInProcess =
        params.rendererAffinity == blpwtk2::Constants::IN_PROCESS_RENDERER;

    if (isInProcess) {
        if (!d_renderProcessHost.get()) {
            DCHECK(-1 == d_inProcessRendererInfo.d_hostId);
            CHECK(d_processHandle != base::kNullProcessHandle);
            d_renderProcessHost.reset(
                new ManagedRenderProcessHost(
                    d_processHandle,
                    profileHost->browserContext()));
            d_inProcessRendererInfo.d_hostId = d_renderProcessHost->id();
            Send(new BlpControlMsg_SetInProcessRendererChannelName(
                d_renderProcessHost->channelId()));
        }

        DCHECK(-1 != d_inProcessRendererInfo.d_hostId);
        hostAffinity = d_inProcessRendererInfo.d_hostId;
    }
    else {
        hostAffinity =
            d_rendererInfoMap->obtainHostAffinity(params.rendererAffinity);
    }

    WebViewHost* webViewHost = new WebViewHost(this,
                    profileHost->browserContext(),
                    params.routingId,
                    isInProcess,
                    (NativeView)params.parent,
                    hostAffinity,
                    params.initiallyVisible,
                    params.properties);

    if (Statics::webViewHostObserver) {
        Statics::webViewHostObserver->webViewHostCreated(
            channelId(),
            webViewHost->getRoutingId(),
            webViewHost->getWebView());
    }
}

void ProcessHostImpl::onWebViewDestroy(int routingId)
{
    LOG(INFO) << "onWebViewDestroy routingId(" << routingId << ")";
    WebViewHost* webViewHost =
        static_cast<WebViewHost*>(findListener(routingId));
    DCHECK(webViewHost);
    if (Statics::webViewHostObserver) {
        Statics::webViewHostObserver->webViewHostDestroyed(
            channelId(),
            webViewHost->getRoutingId());
    }
    delete webViewHost;
}

void ProcessHostImpl::onDumpDiagnoticInfo(int infoType, const std::string& path)
{
    if (infoType == Toolkit::DIAGNOSTIC_INFO_GPU) {
        DumpGpuInfo(path);
    }
}

void ProcessHostImpl::onSetDefaultPrinterName(const std::string& printerName)
{
    printing::PrintBackend::SetUserDefaultPrinterName(printerName);
}

}  // close namespace blpwtk2
