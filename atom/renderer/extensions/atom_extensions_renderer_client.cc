// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/renderer/extensions/atom_extensions_renderer_client.h"

#include <string>
#include "atom/renderer/extensions/atom_extensions_dispatcher_delegate.h"
#include "base/strings/stringprintf.h"
#include "content/public/renderer/render_thread.h"
#include "extensions/common/manifest_handlers/background_info.h"
#include "extensions/common/manifest_handlers/web_accessible_resources_info.h"
#include "extensions/common/permissions/permissions_data.h"
#include "extensions/renderer/dispatcher.h"
#include "extensions/renderer/extension_frame_helper.h"
#include "extensions/renderer/extension_helper.h"
#include "extensions/renderer/extensions_renderer_client.h"
#include "extensions/renderer/extensions_render_frame_observer.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/common/extensions/chrome_manifest_url_handlers.h"
#include "chrome/common/url_constants.h"
#include "extensions/common/constants.h"
#include "third_party/WebKit/public/web/WebConsoleMessage.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebFrame.h"
#include "third_party/WebKit/public/web/WebLocalFrame.h"


namespace extensions {

class RendererPermissionsPolicyDelegate
    : public PermissionsData::PolicyDelegate {
 public:
  explicit RendererPermissionsPolicyDelegate(Dispatcher* dispatcher) {
    PermissionsData::SetPolicyDelegate(this);
  }
  ~RendererPermissionsPolicyDelegate() override {
    PermissionsData::SetPolicyDelegate(NULL);
  };

  bool CanExecuteScriptOnPage(const Extension* extension,
                              const GURL& document_url,
                              int tab_id,
                              int process_id,
                              std::string* error) override {
    return true;
  };

 private:
  Dispatcher* dispatcher_;

  DISALLOW_COPY_AND_ASSIGN(RendererPermissionsPolicyDelegate);
};

AtomExtensionsRendererClient::AtomExtensionsRendererClient() {}

// static
AtomExtensionsRendererClient* AtomExtensionsRendererClient::GetInstance() {
  static base::LazyInstance<AtomExtensionsRendererClient> client =
    LAZY_INSTANCE_INITIALIZER;
  return client.Pointer();
}

bool AtomExtensionsRendererClient::IsIncognitoProcess() const {
  return false;
}

int AtomExtensionsRendererClient::GetLowestIsolatedWorldId() const {
  return chrome::ISOLATED_WORLD_ID_EXTENSIONS;
}

void AtomExtensionsRendererClient::RenderThreadStarted() {
  content::RenderThread* thread = content::RenderThread::Get();
  extension_dispatcher_delegate_.reset(
      new extensions::AtomExtensionsDispatcherDelegate());
  // ChromeRenderViewTest::SetUp() creates its own ExtensionDispatcher and
  // injects it using SetExtensionDispatcher(). Don't overwrite it.
  if (!extension_dispatcher_) {
    extension_dispatcher_.reset(
        new extensions::Dispatcher(extension_dispatcher_delegate_.get()));
  }
  permissions_policy_delegate_.reset(
      new extensions::RendererPermissionsPolicyDelegate(
          extension_dispatcher_.get()));

  thread->AddObserver(extension_dispatcher_.get());
}

void AtomExtensionsRendererClient::RenderFrameCreated(
    content::RenderFrame* render_frame) {
  new extensions::ExtensionsRenderFrameObserver(render_frame);
  new extensions::ExtensionFrameHelper(render_frame,
                                       extension_dispatcher_.get());
  extension_dispatcher_->OnRenderFrameCreated(render_frame);
}

void AtomExtensionsRendererClient::RenderViewCreated(
    content::RenderView* render_view) {
  new extensions::ExtensionHelper(render_view, extension_dispatcher_.get());
}

bool AtomExtensionsRendererClient::OverrideCreatePlugin(
    content::RenderFrame* render_frame,
    const blink::WebPluginParams& params) {
  return false;
}

bool AtomExtensionsRendererClient::AllowPopup() {
  extensions::ScriptContext* current_context =
      extension_dispatcher_->script_context_set().GetCurrent();
  if (!current_context || !current_context->extension())
    return false;

  // See http://crbug.com/117446 for the subtlety of this check.
  switch (current_context->context_type()) {
    case extensions::Feature::UNSPECIFIED_CONTEXT:
    case extensions::Feature::WEB_PAGE_CONTEXT:
    case extensions::Feature::UNBLESSED_EXTENSION_CONTEXT:
    case extensions::Feature::WEBUI_CONTEXT:
    case extensions::Feature::SERVICE_WORKER_CONTEXT:
      return false;
    case extensions::Feature::BLESSED_EXTENSION_CONTEXT:
    case extensions::Feature::CONTENT_SCRIPT_CONTEXT:
      return true;
    case extensions::Feature::BLESSED_WEB_PAGE_CONTEXT:
      return !current_context->web_frame()->parent();
    default:
      NOTREACHED();
      return false;
  }
}

bool AtomExtensionsRendererClient::WillSendRequest(
    blink::WebFrame* frame,
    ui::PageTransition transition_type,
    const GURL& url,
    GURL* new_url) {
  const Extension* extension =
      RendererExtensionRegistry::Get()->GetExtensionOrAppByURL(url);
  if (!extension) {
    // Allow the load in the case of a non-existent extension. We'll just get a
    // 404 from the browser process.
    return false;
  }

  if (!WebAccessibleResourcesInfo::IsResourceWebAccessible(
          extension, url.path())) {
    GURL frame_url = frame->document().url();

    // The page_origin may be GURL("null") for unique origins like data URLs,
    // but this is ok for the checks below.  We only care if it matches the
    // current extension or has a devtools scheme.
    GURL page_origin = GURL(frame->top()->securityOrigin().toString());

    // allow access to background pages and scripts
    // content scripts are whitelisted by default
    bool is_generated_background_page =
        url == BackgroundInfo::GetBackgroundURL(extension);

    // - extensions requesting their own resources (frame_url check is for
    //     images, page_url check is for iframes)
    bool is_own_resource = frame_url.GetOrigin() == extension->url() ||
                           page_origin == extension->url();
    // - unreachable web page error page (to allow showing the icon of the
    //   unreachable app on this page)
    bool is_error_page = frame_url == GURL(content::kUnreachableWebDataURL);

    if (!is_generated_background_page && !is_own_resource && !is_error_page) {
      std::string message = base::StringPrintf(
          "Denying load of %s. Resources must be listed in the "
          "web_accessible_resources manifest key in order to be loaded by "
          "pages outside the extension.",
          url.spec().c_str());
      frame->addMessageToConsole(
          blink::WebConsoleMessage(blink::WebConsoleMessage::LevelError,
                                    blink::WebString::fromUTF8(message)));
      *new_url = GURL("chrome-extension://invalid/");
      return true;
    }
  }

  return false;
}

}  // namespace extensions
