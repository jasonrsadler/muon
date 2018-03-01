'use strict'

const ipcMain = require('electron').ipcMain
const webContents = require('electron').webContents

let supportedWebViewEvents = [
  'tab-replaced-at',
  'load-start',
  'did-attach',
  'set-window',
  'guest-created',
  'guest-ready',
  'will-detach',
  'did-detach',
  'did-finish-load',
  'did-fail-provisional-load',
  'did-fail-load',
  'dom-ready',
  'preferred-size-changed',
  'console-message',
  'did-navigate',
  'did-navigate-in-page',
  'security-style-changed',
  'close',
  'gpu-crashed',
  'plugin-crashed',
  'will-destroy',
  'destroyed',
  'page-favicon-updated',
  'enter-html-full-screen',
  'leave-html-full-screen',
  'media-started-playing',
  'media-paused',
  'found-in-page',
  'did-change-theme-color',
  'update-target-url',
  'context-menu',
  'enable-pepper-menu',
  'repost-form-warning',
  'content-blocked',
  'show-autofill-settings',
  'update-autofill-popup-data-list-values',
  'hide-autofill-popup',
  'show-autofill-popup',
  'did-run-insecure-content',
  'did-block-run-insecure-content'
]

let guests = {}
// ensure webcontent events (from guest) are fired to embedder (window) so they can be forwarded to webview / interested-party
const registerGuest = function (guest, embedder) {
  const tabId = guest.getId()
  
  const oldEmbedderDetails = guests[tabId]
  
  const isNewEmbedder = !oldEmbedderDetails || oldEmbedderDetails.embedder !== embedder

  // Events were previously setup and we don't have a different embedder
  if (oldEmbedderDetails && !isNewEmbedder) {
    return
  }

  if (!oldEmbedderDetails) {
    guests[tabId] = {}
  }
  // always update the embedder so events get routed to the correct window
  guests[tabId].embedder = embedder
  // listen for destroyed event on new embedders
  const destroyedListener = function () {
    if (guests[tabId] && guests[tabId].embedder === embedder) {
      const guestHandlers = guests[tabId].guestHandlers
      guests[tabId] = false
      // remove handlers
      // since now that the reference is reset,
      // next time tab is attached, new handlers will be created
      if (guest && !guest.isDestroyed()) {
        for (const event in guestHandlers) {
          guest.removeListener(event, guestHandlers[event])
        }
      }
    }
  }
  embedder.once('destroyed', destroyedListener)

  // Events were previously setup, but we have a new embedder
  if (oldEmbedderDetails && isNewEmbedder) {
    if (oldEmbedderDetails.embedder && oldEmbedderDetails.destroyedListener && !oldEmbedderDetails.embedder.isDestroyed())
    // stop listening to destroyed event on the embedder
    oldEmbedderDetails.embedder.removeListener('destroyed', oldEmbedderDetails.destroyedListener)
    // remember the new embedders destroy listener, so we can remove that
    // next time the embedder changes
    guests[tabId].destroyedListener = destroyedListener
    // no need to re-attach event listeners if we've already done that, the events
    // will dispatch to the new embedder when they fire
    return
  }

  guests[tabId].destroyedListener = destroyedListener
  
  // Dispatch events to embedder.
  const guestHandlers = { }
  for (const event of supportedWebViewEvents) {
    guestHandlers[event] = function (_, ...args) {
      const embedder = guests[tabId] && guests[tabId].embedder

      if (!embedder || embedder.isDestroyed())
        return

      let forceSend = false
      if (['destroyed'].includes(event)) {
        delete guests[tabId]
        forceSend = true
      }

      if (guest.isDestroyed() && !forceSend) {
        return
      }

      embedder.send.apply(embedder, ['ELECTRON_GUEST_VIEW_INTERNAL_DISPATCH_EVENT', tabId, event].concat(args))
    }
    guest.on(event, guestHandlers[event])
  }
  const handleIpcMessage = function (_, [channel, ...args]) {
    const embedder = guests[tabId] && guests[tabId].embedder
    if (!embedder || embedder.isDestroyed() || guest.isDestroyed())
      return

    embedder.send.apply(embedder, ['ELECTRON_GUEST_VIEW_INTERNAL_IPC_MESSAGE', tabId, channel].concat(args))
  }
  // Dispatch guest's IPC messages to embedder.
  guest.on('ipc-message-host', handleIpcMessage)
  guestHandlers['ipc-message-host'] = handleIpcMessage
  // store handlers so we can deregister them when:
  // - embedder is destroyed
  // - guest does not have an embedder at all
  guests[tabId].guestHandlers = guestHandlers
}

const deregisterGuest = function (guest) {
  if (!guest || guest.isDestroyed()) {
    return
  }
  const tabId = guest.getId()
  const embedderDetails = guests[tabId]
  if (!embedderDetails) {
    return
  }
  guests[tabId] = false
  const guestHandlers = guests[tabId].guestHandlers
  if (guestHandlers) {
    for (const event in guestHandlers) {
      guest.removeListener(event, guestHandlers[event])
    }
  }
  if (embedderDetails.embedder && embedderDetails.destroyedListener && !embedderDetails.embedder.isDestroyed()) {
    embedderDetails.embedder.removeListener('destroyed', embedderDetails.destroyedListener)
  }
}

exports.registerGuest = registerGuest
exports.deregisterGuest = deregisterGuest
