"use strict";

(async () => {
  const product = document.querySelector(".product");
  product.append(document.querySelector("#ytdlp-redesign-window").content.cloneNode(true));
  const q = selector => product.querySelector(selector);
  const all = selector => Array.from(product.querySelectorAll(selector));
  const candidateTemplate = q(".candidate").cloneNode(true), jobTemplate = q(".job").cloneNode(true);
  const state = {view: "home", workspace: "home", expanded: new Set(), media: null, source: null, selected: false, parsing: false, parseId: null, selecting: false,
    tasks: new Map(), collections: new Map(), items: [], positions: new Map(), rows: new Map(), queueRevision: 0, picked: new Set(), selectedTask: null,
    groups: new Map(), ungrouped: {groupId: '', items: [], summary: {}}, itemGroups: new Map(), groupRows: new Map(), currentGroup: null,
    editing: null, collectionEditing: null, folder: "", busy: false, settings: null, logs: new Map(),
    sources: new Map(), inputResults: [], batchId: null, batchEvents: new Map(), channels: new Map(), sourceRequests: new Map(), anchor: null, dragId: null, desktop: null};
  let selectionChain = Promise.resolve(), selectionPending = 0, namingDraft = null;
  let namingRule = null, namingRevision = 0, blankConfigAction = null;
  let settingsChain = Promise.resolve(), settingsPending = 0, batchVisibilityTimer = 0, scopePending = false;
  let strings = {}, persistenceError = null, completion = null, finishSaving = false, finishCancelling = false;
  let components = new Map(), currentComponent = 'yt-dlp', componentSaving = false, componentFeedback = '', componentFeedbackError = false;
  let componentUpdates = new Map(), customUpdateConfirmations = new Map(), releaseNotes = null, releaseRequest = 0;
  let processingDraft = {}, processingDefaults = {}, outputBackup = null, panel = null, previewRevision = 0;
  let namingStatus = '', parameterSection = '', outputInputTimer = 0;
  let settingsCategory = 'defaults';
  let about = null, lastLogView = null, taskTitleClick = 0;
  const logRequests = new Map(), logEncoder = new TextEncoder(), logDecoder = new TextDecoder();
  const t = key => strings["web." + key] || key;
  const say = (message, error = false) => {
    q(".feedback").textContent = persistenceError && message !== persistenceError.message ? message + " · " + persistenceError.message : message;
    q('.feedback').title = q('.feedback').textContent;
    q(".feedback").setAttribute("role", error || persistenceError ? "alert" : "status");
  };
  const report = error => say(error.message || t("host_required"), true);
  const statusText = status => t("status." + status);
  const duration = value => value == null ? t("unknown_duration") :
    (value >= 3600 ? Math.floor(value / 3600) + ":" + String(Math.floor(value / 60) % 60).padStart(2, "0") : Math.floor(value / 60)) + ":" + String(Math.floor(value % 60)).padStart(2, "0");
  const channelPickers = document.createElement("div"), inputFeedback = document.createElement("details");
  channelPickers.className = "channel-pickers"; channelPickers.hidden = true;
  q('.source-count').parentElement.before(channelPickers);
  inputFeedback.className = "advanced-block input-results"; inputFeedback.hidden = true;
  inputFeedback.append(document.createElement("summary"), document.createElement("div")); inputFeedback.lastElementChild.className = 'menu-body'; q('.source-state').after(inputFeedback);
  const parseActions = document.createElement('div'), cancelSource = document.createElement('button'), reparseSources = document.createElement('button'), analyzeSource = q('.analyze-link'), pasteSource = document.createElement('button');
  pasteSource.type = 'button'; pasteSource.className = 'button paste-link';
  pasteSource.addEventListener('click', async () => { try { const value = await bridge.request('readClipboard'); q('.link-box textarea').value = value.text; clearInput(); go('source'); analyzeSource.click(); } catch (error) { report(error); } });
  parseActions.className = 'action-pair'; analyzeSource.replaceWith(parseActions); parseActions.append(pasteSource, analyzeSource, cancelSource);
  cancelSource.type = reparseSources.type = 'button'; cancelSource.className = 'button cancel-source-parse cursor-interaction'; reparseSources.className = 'text-button reparse-sources cursor-interaction'; q('.select-none').after(reparseSources);
  const sourceFocusLabel = document.createElement('label'), sourceFocusName = document.createElement('span'), sourceFocus = document.createElement('select');
  sourceFocus.className = 'source-current'; sourceFocusLabel.append(sourceFocusName, sourceFocus); q('.channel-tab').closest('label').before(sourceFocusLabel);
  const sourceAvailability = document.createElement('select'); sourceAvailability.className = 'source-availability'; sourceAvailability.dataset.feature = 'S05'; q('.toggle-range').after(sourceAvailability);
  q('.settings-category[data-category="appearance"] .two-fields label:first-child select').classList.add('language-select');
  q('.settings-category[data-category="appearance"] .two-fields label:last-child select').classList.add('theme-mode');
  q('.jobs').setAttribute('role', 'listbox'); q('.jobs').setAttribute('aria-multiselectable', 'true'); q('.jobs').tabIndex = 0;
  // 固定框架只移动表现节点，业务模型与控件事件仍使用原入口。
  const queuePane = document.createElement('div'); queuePane.className = 'queue-list-pane';
  q('.queue-layout').prepend(queuePane);
  for (const selector of ['.queue-tools', '.queue-select-row', '.batch-bar', '.jobs']) queuePane.append(q(selector));
  q('.desktop-bar').after(q('.finish-countdown'));
  const updateNotice = document.createElement('button'); updateNotice.type = 'button'; updateNotice.className = 'text-button component-update-notice'; updateNotice.hidden = true;
  updateNotice.addEventListener('click', () => { go('settings'); q('[data-settings="updates"]').click(); }); q('.desktop-bar').append(updateNotice);
  const outputPane = document.createElement('div'); outputPane.className = 'output-list-pane'; q('.output-layout').prepend(outputPane);
  outputPane.append(q('.output-form'), q('[data-page="output"] > .view-actions'));
  const previewDetails = document.createElement('details'); previewDetails.className = 'preview-details';
  const previewSummary = document.createElement('summary'); previewSummary.className = 'text-button cursor-interaction';
  const previewBody = document.createElement('div'); previewBody.className = 'menu-body'; previewDetails.append(previewSummary, previewBody); q('.output-preview').append(previewDetails);
  for (const key of ['title', 'filename', 'path', 'output', 'flags', 'arguments', 'quality', 'scope', 'validation']) { const field = document.createElement('label'), name = document.createElement('span'), content = document.createElement('textarea'); field.className = 'field'; name.dataset.previewLabel = key; content.className = 'preview-full-' + key; content.readOnly = true; content.rows = 2; field.append(name, content); previewBody.append(field); }
  const namingScroll = document.createElement('div'); namingScroll.className = 'form-scroll';
  q('[data-page="naming"] .page-top').after(namingScroll);
  for (const selector of ['.scheme-layout', '.naming-advanced', '.bandcamp-naming']) namingScroll.append(q(selector));
  q('.candidate-list').tabIndex = 0;
  const detailTools = document.createElement('div'), detailActions = document.createElement('div'); detailTools.className = 'detail-tools'; detailActions.className = 'detail-actions';
  q('[data-page="detail"] .page-top').after(detailTools); q('[data-page="detail"]').append(detailActions);
  for (const element of all('.detail-panel')) {
    const toolbar = element.querySelector(':scope > .selection-tools'), footer = element.querySelector(':scope > .view-actions');
    if (toolbar) { toolbar.dataset.detailTools = element.dataset.detail; detailTools.append(toolbar); }
    if (footer) { footer.dataset.detailActions = element.dataset.detail; footer.dataset.feature = element.dataset.feature; detailActions.append(footer); }
    if (element.dataset.detail === 'sections') { const actions = document.createElement('footer'); actions.className = 'view-actions'; actions.dataset.detailActions = 'sections'; actions.dataset.feature = element.dataset.feature; actions.append(element.querySelector(':scope > .action-pair')); detailActions.append(actions); }
  }
  const sourceKind = document.createElement('span'); q('.source-tag').replaceChildren(sourceKind, q('.source-count'));
  // 共用右栏只呈现现有快照和预览；迁移控件节点保留原选择器与事件。
  const contextSidebar = document.createElement('aside'); contextSidebar.className = 'context-sidebar';
  const contextHeading = document.createElement('h2'); contextHeading.className = 'context-heading'; contextHeading.id = 'context-heading'; contextSidebar.setAttribute('aria-labelledby', contextHeading.id);
  const contextPane = document.createElement('section'); contextPane.className = 'context-pane context-summary';
  const contextFields = document.createElement('dl'), contextValidation = document.createElement('p'), contextNote = document.createElement('p');
  contextValidation.className = 'context-validation'; contextValidation.setAttribute('aria-live', 'polite'); contextNote.className = 'context-note';
  const contextDetails = document.createElement('details'); contextDetails.className = 'context-details';
  const contextDetailsTitle = document.createElement('summary'); contextDetailsTitle.className = 'text-button cursor-interaction';
  const contextDetailsBody = document.createElement('div'); contextDetailsBody.className = 'menu-body'; contextDetails.append(contextDetailsTitle, contextDetailsBody);
  contextPane.append(q('.source-heading'), q('.name-preview-area'), contextFields, contextValidation, contextNote, contextDetails);
  contextSidebar.append(contextHeading, q('.task-inspector'), q('.output-preview'), contextPane); q('.app-body').append(contextSidebar);
  for (const selector of ['.task-inspector', '.output-preview']) q(selector).classList.add('context-pane');
  q('.task-inspector > .eyebrow').hidden = true; q('.preview-target').hidden = true;
  const groupContext = document.createElement('div'); groupContext.className = 'queue-group-context'; groupContext.hidden = true;
  const groupContextName = document.createElement('strong'), groupContextState = document.createElement('span');
  groupContextName.className = groupContextState.className = 'truncate-end';
  groupContext.append(groupContextName, groupContextState); contextHeading.after(groupContext);
  const previewFacts = document.createElement('dl'); previewFacts.className = 'preview-facts'; q('.preview-flags').after(previewFacts);
  for (const key of ['quality', 'scope']) { const label = document.createElement('dt'), value = document.createElement('dd'); label.dataset.contextLabel = key; value.className = 'preview-' + key + ' truncate-end'; previewFacts.append(label, value); }
  const previewValidation = document.createElement('p'); previewValidation.className = 'preview-validation context-validation'; previewValidation.setAttribute('aria-live', 'polite');
  const previewArgumentsLabel = document.createElement('span'); previewArgumentsLabel.className = 'small-label'; previewArgumentsLabel.dataset.contextLabel = 'arguments';
  const previewArgumentsText = document.createElement('pre'); previewArgumentsText.className = 'context-arguments'; q('.scope-note').after(previewValidation, previewArgumentsLabel, previewArgumentsText);
  // 创建与任务编辑共用抽屉，原有控件和 Bridge 入口保持不变。
  const drawer = document.createElement('dialog'), drawerChrome = document.createElement('header'), drawerName = document.createElement('strong'), drawerClose = document.createElement('button'), drawerPages = document.createElement('div');
  drawer.className = 'workbench-drawer'; drawer.setAttribute('aria-labelledby', 'workbench-drawer-name'); drawerName.id = 'workbench-drawer-name';
  drawerChrome.className = 'drawer-chrome'; drawerClose.type = 'button'; drawerClose.className = 'button drawer-close'; drawerClose.dataset.workbenchText = 'close'; drawerPages.className = 'drawer-pages';
  drawerChrome.append(drawerName, drawerClose); drawer.append(drawerChrome, drawerPages); product.append(drawer);
  for (const view of ['source', 'output', 'naming', 'detail']) drawerPages.append(q('[data-page="' + view + '"]'));
  const drawerSummary = document.createElement('details'), drawerSummaryLabel = document.createElement('summary'); drawerSummary.className = 'drawer-summary'; drawerSummaryLabel.dataset.workbenchText = 'preview_scope';
  drawerSummary.append(drawerSummaryLabel, contextSidebar); drawerPages.append(drawerSummary);
  q('.queue-tools').after(groupContext);
  let drawerFocus = null;
  drawerClose.addEventListener('click', cancelDrawer);
  drawer.addEventListener('cancel', event => { event.preventDefault(); cancelDrawer(); });
  for (const button of all('.nav-button')) {
    const label = document.createElement('span'); label.className = 'nav-label';
    for (const node of [...button.childNodes]) if (node.nodeType === Node.TEXT_NODE || node.nodeType === Node.ELEMENT_NODE && !node.matches('i,svg,.queue-count')) label.append(node);
    button.insertBefore(label, button.querySelector('.queue-count'));
  }
  const needsFilter = document.createElement('button'); needsFilter.type = 'button'; needsFilter.className = 'queue-filter'; needsFilter.dataset.filter = 'needs'; needsFilter.dataset.workbenchText = 'needs_filter'; needsFilter.setAttribute('aria-pressed', 'false'); q('.queue-filter[data-filter="done"]').before(needsFilter);
  const queueSearch = document.createElement('input'); queueSearch.type = 'text'; queueSearch.className = 'queue-search'; queueSearch.dataset.workbenchPlaceholder = queueSearch.dataset.workbenchLabel = 'search_tasks'; q('.queue-tool-right').prepend(queueSearch);
  queueSearch.addEventListener('input', clearViewSelection);
  const outputTabs = document.createElement('div'); outputTabs.className = 'workbench-output-tabs'; outputTabs.setAttribute('role', 'group'); outputTabs.dataset.workbenchLabel = 'output_tabs';
  for (const tab of ['common', 'subtitles', 'advanced']) { const button = document.createElement('button'); button.type = 'button'; button.dataset.outputTab = tab; button.dataset.workbenchText = tab; button.setAttribute('aria-pressed', String(tab === 'common')); button.addEventListener('click', () => setOutputTab(tab)); outputTabs.append(button); }
  q('.output-form').before(outputTabs);
  const outputCoreFields = q('.format-select').closest('.two-fields'); outputCoreFields.classList.add('output-core-fields');
  const formatHelp = document.createElement('small'), qualityHelp = document.createElement('small'); formatHelp.className = qualityHelp.className = 'field-help'; formatHelp.dataset.polishText = 'format_help';
  q('.format-select').after(formatHelp); q('.quality-field').append(qualityHelp);
  const outputProcessing = document.createElement('section'), outputProcessingTitle = document.createElement('h2'), m4aField = document.createElement('label'), m4aLabel = document.createElement('span'), outputProcessingNote = document.createElement('p'), formatNotice = document.createElement('p');
  outputProcessing.className = 'form-section output-processing'; outputProcessing.dataset.feature = 'O04'; outputProcessingTitle.dataset.polishText = 'output_processing'; m4aField.className = 'field m4a-processing'; m4aLabel.dataset.polishText = 'm4a_strategy'; outputProcessingNote.className = 'small-note'; formatNotice.className = 'small-note format-change-notice'; formatNotice.hidden = true; formatNotice.setAttribute('role', 'status');
  m4aField.append(m4aLabel, q('.audio-treatment')); outputProcessing.append(outputProcessingTitle, m4aField, outputProcessingNote, formatNotice); outputCoreFields.after(outputProcessing);
  const outputPage = q('[data-page="output"]'), outputScroll = document.createElement('div'); outputScroll.className = 'output-scroll'; outputScroll.tabIndex = 0; outputScroll.dataset.workbenchLabel = 'output_title';
  outputPage.append(outputTabs, outputScroll, q('.output-list-pane > .view-actions')); outputScroll.append(q('.output-layout'));
  const processingSection = q('[data-output-section="processing"]'), timingSection = document.createElement('details'), timingTitle = document.createElement('summary'), timingName = document.createElement('span'), timingSummary = document.createElement('span'), timingBody = document.createElement('div');
  timingSection.className = 'advanced-block'; timingSection.dataset.outputSection = 'timing'; timingSection.dataset.feature = 'O10 O11 D07 D08'; timingTitle.className = 'cursor-interaction'; timingName.dataset.polishText = 'timing'; timingSummary.className = 'section-value timing-summary'; timingBody.className = 'advanced-body';
  timingTitle.append(timingName, timingSummary); timingSection.append(timingTitle, timingBody); processingSection.after(timingSection); processingSection.dataset.feature = 'O08 O09 P03 P04';
  for (const element of [...processingSection.querySelector('.advanced-body').children].slice(2)) timingBody.append(element);
  for (const selector of ['[data-output-section="processing"]', '[data-output-section="timing"]', '[data-output-section="files"]']) q(selector).open = true;
  const advancedInherited = document.createElement('p'); advancedInherited.className = 'small-note advanced-inherited'; advancedInherited.dataset.polishText = 'advanced_inherited'; advancedInherited.hidden = true; q('.advanced-options').prepend(advancedInherited);
  for (const [selector, key] of [['.retry-select', 'retries_help'], ['.concurrency-select', 'fragments_help'], ['[data-extra="rate"]', 'rate_help'], ['[data-extra="chapters"]', 'chapters_help'], ['[data-extra="keyframes"]', 'keyframes_help'], ['[data-extra="writeTime"]', 'write_time_help'], ['[data-flag="liveStart"]', 'live_start_help'], ['[data-flag="keepFiles"]', 'keep_files_help'], ['[data-flag="singleVideo"]', 'single_video_help']]) {
    const control = q(selector), label = control.closest('label'), help = label.querySelector('small') || document.createElement('small'); help.dataset.polishText = key; help.className = 'field-help'; help.id = 'polish-help-' + key; label.append(help); control.setAttribute('aria-describedby', help.id); if (selector === '[data-extra="rate"]') q('[data-extra="rateUnit"]').setAttribute('aria-describedby', help.id);
  }
  q('[data-output-section="files"] .compact-checks').className = 'check-stack';
  let outputTab = 'common';
  function setOutputTab(tab) {
    outputTab = tab; outputScroll.scrollTop = 0;
    all('[data-output-tab]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.outputTab === tab)));
    for (const element of q('.output-form').children) element.hidden = element.classList.contains('advanced-options') ? tab === 'common' : tab !== 'common';
    all('[data-output-section]').forEach(element => { const tracks = ['subtitles', 'formats'].includes(element.dataset.outputSection); element.hidden = tab === 'subtitles' ? !tracks : tracks; if (tab === 'subtitles' && !element.hidden) element.open = true; });
  }
  q('.candidate-list').after(q('.grouping-field'));
  const sourcePage = q('[data-page="source"]'), sourceScroll = document.createElement('div'); sourceScroll.className = 'source-scroll'; sourceScroll.tabIndex = 0; sourceScroll.dataset.workbenchLabel = 'source_title';
  const sourceActions = sourcePage.querySelector(':scope > .view-actions');
  for (const element of [...sourcePage.children]) if (!element.matches('.page-top,.view-actions')) sourceScroll.append(element);
  sourcePage.insertBefore(sourceScroll, sourceActions);
  namingScroll.tabIndex = 0; namingScroll.dataset.workbenchLabel = 'naming_title'; q('.settings-layout').tabIndex = 0; q('.nav-scroll').tabIndex = 0;
  const quickStart = document.createElement('button'); quickStart.type = 'button'; quickStart.className = 'button workbench-quick-start'; quickStart.dataset.workbenchText = 'quick_start'; q('.next-output').before(quickStart);
  quickStart.addEventListener('click', async () => { try { const sourceId = state.source?.sourceId; state.editing = null; outputBackup = options(); const prepared = await bridge.request('prepareTaskOptions', {options: state.settings.newTaskOptions}); if (state.view !== 'source' || state.source?.sourceId !== sourceId) return; loadOptions(prepared); await enqueue(true); } catch (error) { report(error); } });
  const selectionNote = document.createElement('p'); selectionNote.className = 'workbench-reselect-note'; selectionNote.hidden = true; q('.candidate-list').before(selectionNote);
  const defaultsCategory = document.createElement('section'); defaultsCategory.className = 'settings-category'; defaultsCategory.dataset.category = 'defaults'; defaultsCategory.hidden = true;
  defaultsCategory.append(q('[data-category="queue"] > .form-section'));
  const sponsorCategory = q('[data-category="sponsor"]'); defaultsCategory.append(...sponsorCategory.children); sponsorCategory.remove(); q('.settings-groups').prepend(defaultsCategory);
  const defaultsLink = q('[data-settings="sponsor"]'); defaultsLink.dataset.settings = 'defaults'; defaultsLink.dataset.workbenchText = 'download_defaults';
  q('.settings-nav').prepend(defaultsLink, q('[data-settings="queue"]'), q('[data-settings="network"]'), q('[data-settings="updates"]'), q('[data-settings="appearance"]'), q('[data-settings="presets"]'), q('[data-settings="about"]'));
  for (const [category, key] of [['queue', 'scheduler'], ['appearance', 'appearance'], ['presets', 'advanced_presets']]) q('[data-settings="' + category + '"]').dataset.workbenchText = key;
  q('.workbench-source-form').addEventListener('submit', event => { event.preventDefault(); if (state.busy || state.parsing) return; q('.link-box textarea').value = q('.workbench-url').value; loadOptions(state.settings.newTaskOptions); go('source'); parseSource(); });
  q('.workbench-url').addEventListener('keydown', event => { if (event.ctrlKey && event.key === 'Enter') { event.preventDefault(); q('.workbench-source-form').requestSubmit(); } });
  q('.workbench-manage-defaults').addEventListener('click', () => { go('settings'); defaultsLink.click(); });
  q('.workbench-defaults .workbench-badge').remove();
  q('.workbench-defaults h2').dataset.workbenchText = 'future_defaults';
  q('.workbench-manage-defaults').dataset.workbenchText = 'manage_defaults_short';
  q('.workbench-defaults .section-heading').append(q('.workbench-manage-defaults'));
  new ResizeObserver(() => { if (state.workspace === 'home') renderWorkbench(); }).observe(q('.workbench-activity'));
  function cancelDrawer() {
    if (state.busy) return;
    if (state.view === 'detail' && panel?.kind === 'logs') rememberLogView();
    if (state.parsing) cancelSource.click();
    if (namingDraft) { loadOptions(namingDraft); namingDraft = null; namingRule = null; ++namingRevision; }
    if (outputBackup) loadOptions(outputBackup);
    outputBackup = null; state.editing = null; state.collectionEditing = null; panel = null;
    clearInput(); go(state.workspace);
  }
  const scopeConfirm = document.createElement('dialog'), scopeConfirmTitle = document.createElement('h2'), scopeConfirmText = document.createElement('p'), scopeConfirmActions = document.createElement('div'), scopeConfirmCancel = document.createElement('button'), scopeConfirmApply = document.createElement('button');
  scopeConfirm.className = 'workbench-confirm'; scopeConfirmTitle.id = 'workbench-confirm-title'; scopeConfirmTitle.dataset.workbenchText = 'confirm_scope'; scopeConfirm.setAttribute('aria-labelledby', scopeConfirmTitle.id); scopeConfirmText.className = 'workbench-scope-note'; scopeConfirmActions.className = 'action-pair';
  scopeConfirmCancel.type = scopeConfirmApply.type = 'button'; scopeConfirmCancel.className = 'button cancel-scope'; scopeConfirmApply.className = 'button primary confirm-scope'; scopeConfirmCancel.dataset.workbenchText = 'cancel'; scopeConfirmApply.dataset.workbenchText = 'confirm'; scopeConfirmActions.append(scopeConfirmCancel, scopeConfirmApply); scopeConfirm.append(scopeConfirmTitle, scopeConfirmText, scopeConfirmActions); product.append(scopeConfirm);
  let scopeConfirmation = null;
  function confirmScope(text, blocked = false) { if (scopeConfirmation) return Promise.resolve(false); scopeConfirmText.textContent = text; scopeConfirmApply.disabled = blocked; scopeConfirm.showModal(); scopeConfirmCancel.focus(); return new Promise(resolve => scopeConfirmation = resolve); }
  function resolveScope(confirmed) { scopeConfirm.close(); const resolve = scopeConfirmation; scopeConfirmation = null; resolve?.(confirmed); }
  scopeConfirmApply.addEventListener('click', () => resolveScope(true)); scopeConfirmCancel.addEventListener('click', () => resolveScope(false)); scopeConfirm.addEventListener('cancel', event => { event.preventDefault(); resolveScope(false); });
  const sharedScopeMessage = () => t('workbench.shared_confirmation').replace('{count}', state.tasks.size) + '\n' + [...state.tasks.values()].map(task => task.media.title + ' · ' + statusText(task.status)).join('\n');
  function renderWorkbench() {
    if (!state.settings) return;
    const tasks = [...state.tasks.values()], counts = {active: 0, waiting: 0, needs: 0, done: 0};
    for (const task of tasks) { if (['running', 'processing', 'stopping'].includes(task.status)) counts.active++; else if (task.status === 'waiting') counts.waiting++; else if (['failed', 'stopped'].includes(task.status)) counts.needs++; else if (task.status === 'done') counts.done++; }
    for (const [key, value] of Object.entries(counts)) q('[data-workbench-count="' + key + '"]').textContent = value;
    const defaults = state.settings.newTaskOptions;
    q('.workbench-default-output').textContent = defaults.format.toUpperCase() + ' · ' + (defaults.kind === 'video' ? defaults.quality === 'none' ? t('no_preference') : defaults.quality + 'p' : t('audio'));
    q('.workbench-default-folder').textContent = defaults.outputFolder; q('.workbench-default-folder').title = defaults.outputFolder;
    q('.workbench-default-concurrency').textContent = t('workbench.concurrent_tasks').replace('{count}', state.settings.application.scheduler.maxConcurrentDownloads);
    const capacity = Math.max(0, Math.min(4, Math.floor((q('.workbench-activity').clientHeight - 16) / 56)));
    const pending = state.items.map(model).filter(value => value && !['done', 'skipped'].includes(summary(value).status)), attention = pending.slice(0, capacity);
    q('.workbench-activity').replaceChildren(...attention.map(value => {
      const row = document.createElement('div'), type = document.createElement('span'), info = document.createElement('div'), title = document.createElement('button'), meta = document.createElement('small'), badge = document.createElement('span');
      row.className = 'workbench-activity-row'; type.className = 'workbench-type'; type.append(lucide.createElement(lucide.icons[value.collectionId ? 'Folder' : value.options.kind === 'audio' ? 'Music2' : 'Video'], {width: 18, height: 18, 'aria-hidden': 'true'}));
      title.type = 'button'; title.className = 'job-open'; title.textContent = mediaOf(value).title; title.title = title.textContent;
      title.addEventListener('click', () => { state.selectedTask = value.collectionId || value.taskId; openPanel('summary').catch(report); });
      meta.className = 'small-label'; meta.textContent = value.collectionId ? t('collection_count').replace('{count}', value.children.length) : value.options.format.toUpperCase();
      badge.className = 'workbench-badge'; badge.dataset.status = summary(value).status; badge.textContent = statusText(summary(value).status); info.append(title, meta); row.append(type, info, badge); return row;
    }));
    if (!attention.length) { const empty = document.createElement(pending.length ? 'button' : 'p'); empty.className = pending.length ? 'text-button' : 'queue-empty'; empty.textContent = t(pending.length ? 'workbench.all_tasks' : 'workbench.no_attention'); if (pending.length) { empty.type = 'button'; empty.addEventListener('click', () => go('queue')); } q('.workbench-activity').append(empty); }
  }
  const scrollControls = [];
  function updateScrollControls() {
    for (const {list, tools, top, bottom} of scrollControls) {
      const limit = list.scrollHeight - list.clientHeight;
      tools.hidden = list.clientHeight === 0 || limit <= 1;
      top.disabled = list.scrollTop <= 1; bottom.disabled = list.scrollTop >= limit - 1;
    }
  }
  for (const selector of ['.candidate-list', '.jobs']) {
    const content = q(selector), list = selector === '.candidate-list' ? sourceScroll : content, tools = document.createElement('div'); tools.className = 'list-scroll-tools action-pair';
    const buttons = ['top', 'bottom'].map(direction => {
      const button = document.createElement('button'); button.type = 'button'; button.className = 'text-button cursor-interaction';
      const icon = document.createElement('i'); icon.dataset.lucide = direction === 'top' ? 'arrow-up-to-line' : 'arrow-down-to-line'; icon.setAttribute('aria-hidden', 'true'); button.append(icon);
      button.addEventListener('click', () => { list.scrollTop = direction === 'top' ? 0 : list.scrollHeight; updateScrollControls(); }); tools.append(button); return button;
    });
    if (selector === '.candidate-list') q('.selected-count').parentElement.append(tools); else q('.queue-tabs').after(tools);
    scrollControls.push({list, tools, top: buttons[0], bottom: buttons[1]});
    list.addEventListener('scroll', updateScrollControls, {passive: true});
    new ResizeObserver(updateScrollControls).observe(list);
    new MutationObserver(updateScrollControls).observe(content, {childList: true, subtree: true, characterData: true});
  }
  const groupMenu = document.createElement('dialog'); groupMenu.className = 'queue-group-menu command-dialog'; groupMenu.dataset.feature = 'Q16 Q18 Q27';
  const groupMenuTitle = document.createElement('strong'), groupName = document.createElement('input'), groupTarget = document.createElement('select'), groupMessage = document.createElement('p');
  groupName.type = 'text'; groupName.maxLength = 80; groupName.className = 'queue-group-name'; groupTarget.className = 'queue-group-target'; groupMessage.className = 'small-note'; groupMessage.setAttribute('role', 'status');
  const groupMenuActions = document.createElement('div'), groupMenuBody = document.createElement('div'); groupMenuActions.className = 'menu-action-list'; groupMenuBody.className = 'command-dialog-body';
  groupMenuTitle.id = 'queue-group-dialog-title'; groupMenu.setAttribute('aria-labelledby', groupMenuTitle.id);
  groupMenuBody.append(groupName, groupTarget, groupMessage); groupMenu.append(groupMenuTitle, groupMenuBody, groupMenuActions); product.append(groupMenu);
  const groupButton = (key, action, parent) => { const button = document.createElement('button'); button.type = 'button'; button.className = 'menu-item cursor-interaction'; button.dataset.groupLabel = key; button.dataset.groupAction = action; parent.append(button); return button; };
  groupButton('group_create', 'create', q('.queue-operations .menu-action-list'));
  groupButton('group_assign_selection', 'assignSelection', q('.queue-operations .menu-action-list'));
  groupButton('group_manage', 'manage', q('.queue-operations .menu-action-list'));
  groupButton('group_assign', 'assignTask', q('.job-menu .menu-grid'));
  for (const [key, action] of [['apply', 'submit'], ['group_rename', 'rename'], ['group_delete', 'delete'], ['group_up', 'up'], ['group_down', 'down'], ['start', 'start'], ['stop', 'stop'], ['retry', 'retry'], ['group_remove_items', 'remove'], ['cancel', 'cancel']]) groupButton(key, action, groupMenuActions);
  // 范围命令与所选命令分开，配置控件迁入固定设置和具名弹窗。
  const scopeTools = document.createElement('div'); scopeTools.className = 'queue-scope-tools';
  const createGroup = q('.queue-operations [data-group-action="create"]'); createGroup.className = 'button group-create cursor-interaction'; q('.group-filter').after(createGroup);
  const scopeField = q('.queue-scope').closest('label'); scopeField.classList.add('queue-scope-field');
  for (const [action, key] of [['start', 'start_scope'], ['clearDone', 'clear_scope']]) {
    const button = q('.queue-operations [data-batch="' + action + '"]'), label = document.createElement('span'), compact = document.createElement('span');
    button.className = 'button cursor-interaction'; label.className = 'scope-full-label'; label.dataset.polishText = key; compact.className = 'scope-compact-label'; compact.dataset.polishText = action === 'start' ? 'start_compact' : 'clear_compact';
    button.replaceChildren(label, compact); scopeTools.append(button);
  }
  scopeTools.prepend(scopeField, groupContext); scopeTools.append(q('.queue-operations')); q('.queue-select-row').before(scopeTools);
  const scopeMenuLabel = document.createElement('span'), scopeMenuCompact = document.createElement('span'); scopeMenuLabel.className = 'scope-full-label'; scopeMenuLabel.dataset.polishText = 'range_more'; scopeMenuCompact.className = 'scope-compact-label'; scopeMenuCompact.dataset.polishText = 'more_compact';
  q('.queue-operations > summary').replaceChildren(scopeMenuLabel, scopeMenuCompact); q('.batch-more > summary').dataset.polishText = 'selection_more';
  q('.copy-options').dataset.polishText = 'copy_all'; q('.queue-operations [data-batch="remove"]').dataset.polishText = 'remove_scope';
  const selectionMenu = q('.batch-more .menu-body');
  selectionMenu.append(q('.queue-operations [data-group-action="assignSelection"]'), q('.batch-bar [data-batch="remove"]'));
  q('.batch-bar [data-batch="remove"]').className = 'menu-item danger-command cursor-interaction'; q('.batch-bar [data-batch="remove"]').dataset.polishText = 'remove_selection';
  q('.queue-operations [data-batch="remove"]').classList.add('danger-command');
  q('.queue-operations .menu-action-list').append(q('.queue-operations [data-batch="remove"]'));
  const queuePreferences = document.createElement('section'), queuePreferencesTitle = document.createElement('h2'); queuePreferences.className = 'form-section queue-command-preferences'; queuePreferencesTitle.dataset.polishText = 'completion_and_display';
  queuePreferences.dataset.feature = 'Q23 Q24 Q25'; queuePreferences.append(queuePreferencesTitle, q('.finish-action'), q('.queue-display')); q('[data-category="queue"]').append(queuePreferences); q('.queue-display').open = true;
  const availabilityDialog = document.createElement('dialog'), availabilityTitle = document.createElement('h2'), availabilityNote = document.createElement('p'), availabilityActions = document.createElement('div'), availabilityCancel = document.createElement('button'), availabilityOpen = document.createElement('button');
  availabilityDialog.className = 'queue-availability-dialog command-dialog'; availabilityTitle.id = 'queue-availability-title'; availabilityTitle.dataset.polishText = 'downloadable'; availabilityDialog.setAttribute('aria-labelledby', availabilityTitle.id); availabilityNote.className = 'small-note'; availabilityActions.className = 'menu-action-list';
  for (const action of ['skip', 'unskip', 'toggleSkip']) availabilityActions.append(q('.batch-more [data-batch="' + action + '"]'));
  availabilityCancel.type = availabilityOpen.type = 'button'; availabilityCancel.className = 'button'; availabilityCancel.dataset.workbenchText = 'cancel'; availabilityOpen.className = 'menu-item selection-availability cursor-interaction'; availabilityOpen.dataset.polishText = 'downloadable';
  selectionMenu.querySelector('[data-group-action="assignSelection"]').before(availabilityOpen); availabilityDialog.append(availabilityTitle, availabilityNote, availabilityActions, availabilityCancel); product.append(availabilityDialog);
  const selectionFeedback = document.createElement('p'); selectionFeedback.className = 'small-note selection-feedback'; selectionFeedback.setAttribute('role', 'status'); q('.batch-bar').append(selectionFeedback);
  for (const [menu, id] of [[q('.queue-operations .menu-body'), 'queue-scope-menu'], [selectionMenu, 'queue-selection-menu']]) { menu.id = id; menu.classList.add('command-menu'); menu.setAttribute('role', 'menu'); menu.parentElement.querySelector('summary').setAttribute('aria-controls', id); for (const button of menu.querySelectorAll('button')) button.setAttribute('role', 'menuitem'); }
  let availabilityFocus = null;
  function closeAvailability() { availabilityDialog.close(); availabilityFocus?.focus({preventScroll: true}); }
  availabilityOpen.addEventListener('click', () => { availabilityFocus = q('.batch-more > summary'); availabilityNote.textContent = t('selected_tasks').replace('{count}', state.picked.size); availabilityDialog.showModal(); availabilityCancel.focus(); });
  availabilityCancel.addEventListener('click', closeAvailability); availabilityDialog.addEventListener('cancel', event => { event.preventDefault(); closeAvailability(); });
  let activeMenu = null, groupEditing = null;
  function positionMenu() {
    if (!activeMenu) return;
    const {element, anchor} = activeMenu, box = anchor.getBoundingClientRect();
    if (!anchor.isConnected || box.bottom < 0 || box.top > innerHeight || box.right < 0 || box.left > innerWidth) { closeMenu(); return; }
    const below = Math.max(0, innerHeight - Math.max(8, box.bottom + 5) - 8), above = Math.max(0, Math.min(innerHeight - 8, box.top - 5) - 8);
    element.style.maxHeight = Math.max(below, above) + 'px';
    const down = element.offsetHeight <= below || below >= above;
    element.style.maxHeight = (down ? below : above) + 'px';
    const width = element.offsetWidth, height = element.offsetHeight;
    element.style.left = Math.max(8, Math.min(box.right - width, innerWidth - width - 8)) + 'px';
    element.style.top = Math.max(8, Math.min(down ? box.bottom + 5 : box.top - height - 5, innerHeight - height - 8)) + 'px';
  }
  function closeMenu(restoreFocus = true) {
    if (!activeMenu) return;
    const {element, anchor, owner} = activeMenu; activeMenu = null;
    anchor.setAttribute('aria-expanded', 'false');
    if (element.matches(':popover-open')) element.hidePopover();
    if (owner) owner.open = false; else { element.hidden = true; element.setAttribute('aria-expanded', 'false'); }
    if (restoreFocus && anchor.isConnected) anchor.focus({preventScroll: true});
  }
  function openMenu(element, anchor, owner = null) {
    closeMenu(false); if (owner) owner.open = true; element.hidden = false; activeMenu = {element, anchor, owner}; anchor.setAttribute('aria-expanded', 'true');
    element.showPopover(); element.scrollTop = 0; positionMenu();
  }
  for (const element of [...all('.menu-body'), q('.job-menu')]) {
    element.classList.add('anchored-menu'); element.setAttribute('popover', 'auto');
    element.addEventListener('toggle', event => { if (event.newState === 'closed' && activeMenu?.element === element && !element.matches(':popover-open')) closeMenu(false); });
    new ResizeObserver(positionMenu).observe(element);
    const owner = element.parentElement;
    if (owner.tagName === 'DETAILS') owner.addEventListener('toggle', () => { if (owner.open && activeMenu?.owner !== owner) openMenu(element, owner.querySelector('summary'), owner); else if (!owner.open && activeMenu?.owner === owner) closeMenu(); });
    element.addEventListener('click', event => { if (event.target.closest('button:not(:disabled)') && !event.target.closest('[data-group-action]')) closeMenu(); });
    if (element.classList.contains('command-menu')) {
      const anchor = owner.querySelector('summary'); anchor.setAttribute('aria-haspopup', 'menu');
      anchor.addEventListener('keydown', event => { if (!['ArrowDown', 'ArrowUp'].includes(event.key)) return; event.preventDefault(); openMenu(element, anchor, owner); const buttons = [...element.querySelectorAll('button:not(:disabled)')]; (event.key === 'ArrowUp' ? buttons.at(-1) : buttons[0])?.focus(); });
      element.addEventListener('keydown', event => {
        if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); closeMenu(); return; }
        if (event.key === 'Tab') { closeMenu(); return; }
        if (!['ArrowDown', 'ArrowUp', 'Home', 'End'].includes(event.key)) return;
        event.preventDefault(); const buttons = [...element.querySelectorAll('button:not(:disabled)')], index = buttons.indexOf(document.activeElement);
        buttons[event.key === 'Home' ? 0 : event.key === 'End' ? buttons.length - 1 : (index + (event.key === 'ArrowUp' ? -1 : 1) + buttons.length) % buttons.length]?.focus();
      });
    }
  }
  product.addEventListener('scroll', event => { if (!activeMenu || activeMenu.element.contains(event.target)) return; positionMenu(); }, true);
  window.addEventListener('resize', () => { positionMenu(); updateScrollControls(); });
  function openJobMenu(anchor = state.rows.get(state.selectedTask)?.querySelector('.job-more')) {
    if (!anchor) return; renderInspector(); openMenu(q('.job-menu'), anchor); q('.job-menu').setAttribute('aria-expanded', 'true');
  }
  const fullInfo = document.createElement('button'); fullInfo.type = 'button'; fullInfo.className = 'text-button full-task-info cursor-interaction'; fullInfo.dataset.panel = 'summary';
  q('.task-inspector').append(fullInfo);
  const taskContextActions = document.createElement('div'); taskContextActions.className = 'task-context-actions';
  const taskContextAction = document.createElement('button'), taskContextCopy = document.createElement('button');
  taskContextAction.type = taskContextCopy.type = 'button'; taskContextAction.className = 'button context-task-action'; taskContextCopy.className = 'text-button context-task-copy';
  taskContextActions.append(taskContextAction, taskContextCopy); q('.edit-job').after(taskContextActions);
  taskContextAction.addEventListener('click', () => runItemAction(sidebarTaskId()).catch(report));
  taskContextCopy.addEventListener('click', () => copyClipboard({kind: 'urls', ids: [sidebarTaskId()]}).catch(report));
  const detailError = document.createElement('p'); detailError.className = 'detail-error error'; detailError.setAttribute('role', 'alert'); detailError.hidden = true; q('.edit-job').before(detailError);
  const summaryPanel = document.createElement('section'); summaryPanel.className = 'detail-panel task-summary'; summaryPanel.dataset.detail = 'summary'; summaryPanel.hidden = true; q('[data-page="detail"]').append(summaryPanel);
  const detailScroll = document.createElement('div'); detailScroll.className = 'detail-scroll'; detailScroll.tabIndex = 0; detailScroll.dataset.workbenchLabel = 'detail_title';
  for (const element of all('.detail-panel')) detailScroll.append(element);
  q('[data-page="detail"]').insertBefore(detailScroll, detailActions);
  const pathValue = (element, value, filename = false) => {
    element.title = value || ''; element.classList.add('truncate-path');
    const name = document.createElement('span'), ext = document.createElement('span'), match = filename && /^(.*)(\.[^.\\/]+)$/.exec(value || '');
    name.textContent = match ? match[1] : value; ext.textContent = match ? match[2] : ''; element.replaceChildren(name, ext);
  };
  const sidebarTaskId = () => state.view === 'detail' ? panel?.mediaId : state.selectedTask;
  function renderContext() {
    if (!state.settings) return;
    renderGroupContext();
    for (const key of ['Tools', 'Actions']) { const items = all('[data-detail-' + key.toLowerCase() + ']'); items.forEach(element => element.hidden = element.dataset['detail' + key] !== panel?.kind); q('.detail-' + key.toLowerCase()).hidden = !items.some(element => !element.hidden); }
    const taskContext = ['queue', 'detail'].includes(state.view) && model(sidebarTaskId()), outputContext = state.view === 'output';
    q('.task-inspector').hidden = !taskContext; q('.output-preview').hidden = !outputContext; contextPane.hidden = !!taskContext || outputContext;
    q('.source-heading').hidden = state.view !== 'source' || !state.media; q('.name-preview-area').hidden = state.view !== 'naming';
    contextHeading.textContent = t(taskContext ? 'context.task' : outputContext ? parameterSection ? 'context.parameters' : 'context.output' : 'context.' + state.view);
    if (taskContext || outputContext) return;
    let fields = [], full = [], validation = '', note = '', error = false;
    const boolean = value => t(value ? 'context.enabled' : 'context.disabled');
    if (state.view === 'source') {
      const sources = selectionSources(), entries = sources.flatMap(source => source.entries), selected = entries.find(entry => entry.selected);
      fields = [['context.sources', sources.length], ['context.available', sources.reduce((count, source) => count + source.availableCount, 0)], ['context.selected', selectedCount()], ['context.unavailable', entries.filter(entry => !entry.available).length], ['context.selection_preview', selected?.media.title || t('select_media')], ['context.duration', selected ? duration(selected.media.duration) : '---']];
      const channel = state.channels.get(state.source?.sourceId);
      if (state.source?.category || channel) fields.push(['context.source_category', state.source.category ? q('.channel-tab option[value="' + state.source.category + '"]')?.textContent || state.source.category : state.source.metadata.title]);
      const failed = state.inputResults.filter(result => result.error || ['failed', 'invalid'].includes(result.status)), catalogErrors = [...state.channels.values()].filter(value => value.error);
      if (failed.length) fields.push(['context.input_errors', failed.length]);
      validation = q('.source-state').textContent; note = t('context.source_note');
      full = [['context.sources', sources.map(source => source.metadata.title).join('\n')], ['context.selection_preview', entries.filter(entry => entry.selected).map(entry => entry.index + ' · ' + entry.media.title).join('\n')], ['context.unavailable', entries.filter(entry => !entry.available).map(entry => entry.index + ' · ' + entry.media.title + ' · ' + (entry.error?.message || t('entry_unavailable'))).join('\n')]];
      if (failed.length || catalogErrors.length) { const failure = failed.find(result => result.error)?.error || catalogErrors[0]?.error; error = true; validation += ' · ' + t('context.source_errors').replace('{count}', failed.length + catalogErrors.length) + ' · ' + (failure?.diagnostic?.split('\n').find(line => /^ERROR\b/i.test(line)) || failure?.message || ''); full.push(['context.source_diagnostic', [...failed.map(result => result.input + ' · ' + t('input.' + result.status) + ' · ' + (result.error?.message || '') + '\n' + (result.error?.diagnostic || '')), ...catalogErrors.map(value => value.root.metadata.title + ' · ' + value.error.message + '\n' + (value.error.diagnostic || ''))].join('\n')]); }
    } else if (state.view === 'naming') {
      const filename = namingRule?.filename || '', extension = /\.[^.\\/]+$/.exec(filename)?.[0] || '---';
      fields = [['context.extension', extension], ['task_path', namingDraft?.outputFolder || '', 'path'], ['context.scope', t('context.naming_scope')]];
      full = [['task_filename', filename], ['task_path', namingDraft?.outputFolder || ''], ['context.template', namingRule?.outputTemplate || q('.raw-template').value]];
      validation = namingStatus; error = !!namingStatus && ![t('context.checking'), t('context.valid'), t('context.empty_naming')].includes(namingStatus); note = t('context.naming_note');
    } else if (state.view === 'settings') {
      const category = q('.settings-category:not([hidden])')?.dataset.category, app = state.settings.application;
      fields = [['context.category', q('[data-settings="' + category + '"]').textContent]];
      if (category === 'appearance') fields.push(['context.language', t(app.applicationBehavior.language === 'zh-CN' ? 'language_zh' : 'language_en')], ['context.theme', t('theme_' + app.appearance.theme)], ['context.exact_bytes', boolean(app.appearance.exactBytes)]);
      if (category === 'queue') fields.push(['context.defaults', q('.default-output-summary').textContent], ['context.download_slots', app.scheduler.maxConcurrentDownloads], ['context.metadata_slots', app.scheduler.maxConcurrentMetadata], ['context.scope', t(app && state.settings.taskOptionsScope.mode === 'shared' ? 'shared_edit_scope' : 'default_scope_individual')]);
      if (category === 'network') fields.push(['context.proxy', boolean(app.network.proxyEnabled)], ['context.cookies', q('.cookie-mode option[value="' + app.authentication.mode + '"]').textContent], ['context.downloader', boolean(app.externalDownloader.enabled) + ' · ' + q('.aria-status').textContent]);
      if (category === 'defaults') fields.push(['context.mark', boolean(app.contentProcessing.sponsorMark) + ' · ' + app.contentProcessing.markCategories.length], ['context.remove', boolean(app.contentProcessing.sponsorRemove) + ' · ' + app.contentProcessing.removeCategories.length]);
      if (category === 'presets') fields.push(['context.configs', state.settings.applicationPresets.length], ['context.download_presets', state.settings.presets.length], ['context.naming_presets', state.settings.namingPresets.length]);
      validation = q('.settings-save-state').textContent; error = q('.settings-save-state').getAttribute('role') === 'alert';
      fields.push(['context.restart', t(state.settings.diagnostics.restartRequired ? 'settings_restart_required' : 'context.no_restart')]);
      note = t('context.settings_' + category); full = fields;
      if (category === 'about') { fields = [['about.programVersion', about?.programVersion || t('about.unknown')], ['about.architecture', about?.architecture || t('about.unknown')]]; full = fields; validation = ''; error = false; }
      if (category === 'updates') {
        const component = components.get(currentComponent);
        if (component) {
          contextHeading.textContent = componentName(component.component);
          fields = [['component.current', componentVersion(component)], ['component.latest', component.latestVersion || t('component.not_checked')], ['component.status', componentStatus(component)], ['component.effective_path', component.path || t('component.notInstalled'), 'path']];
          if (component.channel) fields.push(['component.channel', component.channel === 'stable' ? t('component.stable') : t('component.nightly')]);
          fields.push(['component.source', component.source || t('component.no_source')], ['component.checked_at', component.checkedAt ? new Date(component.checkedAt).toLocaleString(document.documentElement.lang) : t('component.not_checked')]);
          validation = [componentFeedback, componentError(component)].filter(Boolean).join(' · '); error = componentFeedbackError || !!component.error || !!component.localError;
          note = t(component.component === 'ffmpeg' ? 'component.ffmpeg_note' : component.component === 'gui' ? 'component.gui_note' : 'component.path_timing');
          full = [...fields, ['component.installation', t('component.installation.' + component.installation)], ['component.companion_path', component.companionPath || ''], ['component.status', componentStatus(component) + ' · ' + componentError(component)], ['component.source', component.testSource ? t('component.test_source') + ' · ' + component.source : component.source], ['component.http_status', component.httpStatus || t('component.not_checked')], ['component.diagnostics', [component.error, component.localError].filter(Boolean).map(value => value.code + ' · ' + (value.systemError || 0)).join(' / ')]];
        }
      }
    } else if (state.view === 'detail' && panel?.kind === 'changes') {
      const release = releaseNotes?.releases[Number(q('.release-version').value)]; contextHeading.textContent = t('update.releaseNotes');
      fields = [['update.component', componentName(releaseNotes?.component || 'yt-dlp')], ['update.version', release?.tag || '---'], ['component.source', release?.url || t('component.no_source')]];
      validation = q('.release-status').textContent; error = !!releaseNotes?.error; note = t('update.notesAsText'); full = fields;
    } else if (state.view === 'detail') {
      fields = [['task_title', panel?.details?.metadata.title || q('.panel-context').textContent], ['context.category', q('.panel-title').textContent], ['task_output', panel?.options?.format.toUpperCase() || '---'], ['context.scope', t('context.draft_scope')]];
      const message = panel?.kind === 'sections' ? q('.section-error').textContent : '';
      validation = message || t('context.draft_note'); error = !!message; note = t('context.detail_note'); full = fields;
    } else {
      fields = [['context.tasks', state.items.length], ['context.selection_preview', t('select_task_first')]]; note = t('context.queue_note'); full = fields;
    }
    contextFields.replaceChildren(...fields.flatMap(([label, text, kind]) => { const name = document.createElement('dt'), value = document.createElement('dd'); name.textContent = t(label); value.textContent = String(text); value.title = String(text); if (kind === 'path') pathValue(value, String(text)); else value.className = 'truncate-end'; return [name, value]; }));
    contextValidation.textContent = validation; contextValidation.title = validation; contextValidation.hidden = !validation; contextValidation.setAttribute('role', error ? 'alert' : 'status');
    contextNote.textContent = note; contextNote.title = note;
    contextDetailsBody.replaceChildren(...[...full, ['context.validation', validation], ['context.notes', note]].filter(([, text]) => text !== '').map(([label, text]) => { const field = document.createElement('label'), name = document.createElement('span'), value = document.createElement('textarea'); field.className = 'field'; name.textContent = t(label); value.readOnly = true; value.rows = 2; value.value = String(text); field.append(name, value); return field; }));
    contextDetailsTitle.textContent = t('context.full_info');
  }
  const baseColors = Object.fromEntries(["paper", "surface", "rail", "ink", "muted-ink", "line", "soft", "brand", "on-brand", "active", "success", "error"].map(key => [key, product.style.getPropertyValue("--" + key)]));
  function applyAppearance() {
    if (!state.settings || !state.desktop) return;
    const appearance = state.settings.application.appearance, theme = appearance.theme === "system" ? state.desktop.systemTheme : appearance.theme;
    document.documentElement.style.colorScheme = theme;
    for (const [key, value] of Object.entries(baseColors)) if (value) product.style.setProperty("--" + key, value); else product.style.removeProperty("--" + key);
    if (appearance.contrast) {
      const computed = getComputedStyle(product), colors = Object.fromEntries(Object.keys(baseColors).map(key => [key, computed.getPropertyValue("--" + key)]));
      const ink = theme === "dark" ? "white" : "black", paper = theme === "dark" ? "black" : "white";
      for (const [key, value] of Object.entries(colors)) product.style.setProperty("--" + key, `color-mix(in srgb, ${value} ${100 - appearance.contrast}%, ${["ink", "muted-ink", "line"].includes(key) ? ink : paper})`);
    }
    q('.contrast-value').value = appearance.contrast + "%";
  }
  function sizeText(bytes) {
    if (bytes == null) return "";
    if (state.settings.application.appearance.exactBytes) return t("exact_size").replace("{count}", Number(bytes).toLocaleString(document.documentElement.lang));
    const units = ["B", "KiB", "MiB", "GiB", "TiB"]; let value = Number(bytes), unit = 0;
    while (value >= 1024 && unit < units.length - 1) { value /= 1024; ++unit; }
    return value.toFixed(unit ? 2 : 0) + " " + units[unit];
  }
  async function copyClipboard(payload) { await bridge.request("copyClipboard", payload); say(t("clipboard_succeeded")); }
  function cover(element, media) {
    element.replaceChildren();
    element.style.padding = "";
    element.setAttribute("aria-label", media?.title || t("no_cover"));
    if (media?.thumbnail && /^https?:\/\//i.test(media.thumbnail)) {
      element.style.padding = "0";
      const image = new Image(); image.alt = media.title; image.src = media.thumbnail; image.draggable = false;
      image.addEventListener("error", () => { element.style.padding = ""; element.textContent = t("no_cover"); }, {once: true});
      element.append(image);
    } else element.textContent = t("no_cover");
  }
  function go(view) {
    clearTimeout(taskTitleClick);
    closeMenu();
    if (state.view === 'detail' && panel?.kind === 'logs') rememberLogView();
    const previousView = state.view; state.view = view; drawer.dataset.view = view;
    const overlay = ['source', 'output', 'naming', 'detail'].includes(view);
    if (!overlay) { if (state.workspace !== view && view === 'queue') q('.jobs').scrollTop = 0; state.workspace = view; }
    product.dataset.workspace = state.workspace;
    all(".view").forEach(el => el.hidden = el.dataset.page !== view && (!overlay || el.dataset.page !== state.workspace));
    all(".nav-button").forEach(el => { const selected = el.dataset.go === state.workspace; el.setAttribute("aria-pressed", String(selected)); if (selected) el.setAttribute('aria-current', 'page'); else el.removeAttribute('aria-current'); });
    all('[data-settings]').forEach(button => { const selected = state.workspace === 'settings' && button.dataset.settings === settingsCategory; button.setAttribute('aria-pressed', String(selected)); if (selected) button.setAttribute('aria-current', 'page'); else button.removeAttribute('aria-current'); });
    const categoryName = q('[data-settings="' + settingsCategory + '"]').textContent;
    q('[data-page="settings"] h1').textContent = categoryName; q('.settings-layout').setAttribute('aria-label', categoryName);
    q('.workbench-breadcrumb').textContent = 'yt-dlp interface / ' + t('workbench.' + state.workspace) + (state.workspace === 'settings' ? ' / ' + categoryName : '');
    q('.workbench-page-name').textContent = t('workbench.' + state.workspace + (state.workspace === 'settings' ? '' : '_title'));
    if (overlay) {
      drawerName.textContent = t('workbench.' + view + '_title'); drawerSummary.hidden = view === 'source';
      const scroll = view === 'output' ? outputScroll : view === 'source' ? sourceScroll : view === 'naming' ? namingScroll : detailScroll;
      scroll.append(drawerSummary);
      if (view === 'detail') drawerSummary.open = true;
      if (!drawer.open) { drawerFocus = document.activeElement; drawer.showModal(); }
      if (view === 'detail') {
        detailScroll.dataset.kind = panel?.kind;
        const saved = panel?.kind === 'logs' && lastLogView?.taskId === panel.mediaId && lastLogView.executionId === state.logs.get(panel.mediaId)?.executionId ? lastLogView : null;
        scroll.scrollTop = saved?.atEnd ? scroll.scrollHeight : saved?.top || 0;
      } else if (view !== 'output') scroll.scrollTop = 0;
      if (view === 'output') setOutputTab(['detail', 'naming'].includes(previousView) ? outputTab : 'common');
    } else if (drawer.open) { drawer.close(); if (drawerFocus?.isConnected) drawerFocus.focus({preventScroll: true}); }
    if (view !== 'queue' && view !== 'detail') { state.picked.clear(); state.anchor = null; }
    q('.job-menu').hidden = true;
    renderInspector(); renderContext(); renderWorkbench(); updateScrollControls();
  }
  function options() {
    return {kind: q('input[name="download-kind"]:checked').value, format: q(".format-select").value,
      quality: q(".quality-select").value, cover: q(".cover-check").checked,
      metadata: q(".metadata-check").checked, outputFolder: q(".folder-input").value,
      naming: q(".naming-select").value, outputTemplate: q(".naming-select").value === "custom" ? q(".raw-template").value : "",
      bandcampTemplate: q(".bandcamp-template").value,
      playlistPrefix: q(".playlist-prefix").value, zeroPadding: q('[data-extra="zeroPadding"]').checked,
      playlistFolder: q('[data-extra="playlistFolder"]').checked, singleVideo: q('[data-flag="singleVideo"]').checked,
      processing: {...processingDraft, videoCodec: q('[data-choice="videoCodec"]').value, audioCodec: q('[data-choice="audioCodec"]').value,
        m4aMode: q(".audio-treatment").value, higherFps: q('[data-extra="higherFps"]').checked, premium: q('[data-extra="premium"]').checked,
        rate: q('[data-extra="rate"]').value, rateUnit: q('[data-extra="rateUnit"]').value, chapters: q('[data-extra="chapters"]').value,
        keyframes: q('[data-extra="keyframes"]').checked, writeTime: q('[data-extra="writeTime"]').checked,
        subtitles: q(".subtitle-select").value, retries: q(".retry-select").value, fragments: q(".concurrency-select").value,
        rawEnabled: q(".raw-enabled").checked, rawArgs: q(".raw-input").value,
        ...Object.fromEntries(["embedSubs", "keepFiles", "windowsNames", "saveCover", "saveInfo", "saveDescription", "saveComments", "liveStart"].map(key => [key, q('[data-flag="' + key + '"]').checked]))}};
  }
  function loadOptions(value) {
    formatNotice.hidden = true; formatNotice.textContent = '';
    q('input[name="download-kind"][value="' + value.kind + '"]').checked = true;
    setKind(value.kind, value.format);
    q(".quality-select").value = value.quality;
    q(".cover-check").checked = value.cover;
    q(".metadata-check").checked = value.metadata;
    q(".folder-input").value = value.outputFolder;
    q(".naming-select").value = value.naming;
    q(".raw-template").value = value.outputTemplate || namingTemplate(value.naming);
    q(".bandcamp-template").value = value.bandcampTemplate ?? state.settings.naming.bandcampDefault;
    q(".playlist-prefix").value = value.playlistPrefix ?? "%(playlist_index)d - ";
    q('[data-extra="zeroPadding"]').checked = value.zeroPadding ?? true;
    q('[data-extra="playlistFolder"]').checked = value.playlistFolder ?? true;
    q('[data-flag="singleVideo"]').checked = value.singleVideo ?? false;
    processingDraft = structuredClone(value.processing || state.settings.defaults.processing);
    for (const [key, selector] of Object.entries({videoCodec: '[data-choice="videoCodec"]', audioCodec: '[data-choice="audioCodec"]', m4aMode: ".audio-treatment", subtitles: ".subtitle-select", retries: ".retry-select", fragments: ".concurrency-select", rawArgs: ".raw-input"})) q(selector).value = processingDraft[key];
    for (const key of ["rate", "rateUnit", "chapters"]) q('[data-extra="' + key + '"]').value = processingDraft[key];
    for (const key of ["higherFps", "premium", "keyframes", "writeTime"]) q('[data-extra="' + key + '"]').checked = processingDraft[key];
    for (const key of ["embedSubs", "keepFiles", "windowsNames", "saveCover", "saveInfo", "saveDescription", "saveComments", "liveStart"]) q('[data-flag="' + key + '"]').checked = processingDraft[key];
    q(".raw-enabled").checked = processingDraft.rawEnabled;
    renderOutput();
  }
  const namingTemplate = value => state.settings.naming.templates[value] || state.settings.naming.templates.title;
  const model = id => state.collections.get(id) || state.tasks.get(id);
  function selectionSources() {
    if (state.collectionEditing || !state.inputResults.length) return state.source ? [state.source] : [];
    return state.inputResults.map(result => state.sources.get(state.batchEvents.get(result.requestId)?.source?.sourceId)).filter(Boolean);
  }
  const selectedCount = () => selectionSources().reduce((count, source) => count + source.selectedEntries.length, 0);
  function setKind(kind, format = kind === "video" ? "mp4" : "mp3") {
    const select = q(".format-select");
    select.replaceChildren(...(kind === "video" ? ["none", "mp4", "webm", "mkv"] : ["none", "m4a", "mp3", "ogg", "webm", "flac", "opus"]).map(key => new Option(key === "none" ? t("original_format") : key.toUpperCase(), key)));
    select.value = format;
    q(".quality-select").hidden = kind !== "video";
    q(".quality-value").hidden = kind === "video";
    q(".quality-label").textContent = t(kind === "video" ? "quality_limit" : "audio_quality");
    q(".audio-treatment").hidden = kind !== "audio" || select.value !== "m4a";
    m4aField.hidden = q('.audio-treatment').hidden;
  }
  function renderOutput() {
    const sources = selectionSources(), selected = sources.find(source => source.selectedEntries.length);
    const value = options(), editing = model(state.editing), media = editing?.source?.metadata || editing?.media ||
      (sources.length > 1 ? selected?.kind === "media" ? selected.entries[0]?.media : selected?.metadata : state.media);
    q(".preview-format").textContent = (value.format === 'none' ? t('original_format') : value.format.toUpperCase()) + " · " + t(value.kind === "video" ? "video" : "audio");
    q(".preview-flags").textContent = [value.cover && t("embed_cover"), value.metadata && t("embed_metadata")].filter(Boolean).join(" · ");
    q(".preview-folder").textContent = value.outputFolder;
    pathValue(q('.preview-folder'), value.outputFolder);
    q('.preview-full-title').value = media?.title || ''; q('.preview-full-path').value = value.outputFolder; q('.preview-full-output').value = q('.preview-format').textContent; q('.preview-full-flags').value = q('.preview-flags').textContent;
    q('.preview-flags').title = q('.preview-flags').textContent;
    q(".output-scope").textContent = !state.editing && sources.length > 1 ? t("multiple_sources") : media?.title || "";
    q(".output-count").textContent = editing?.collectionId ? t("collection_defaults_only") : state.editing ? t("current_task") : t("selected_media").replace("{count}", selectedCount());
    if (state.editing !== 'defaults' && state.settings.taskOptionsScope.mode === 'shared') q('.output-count').textContent += ' · ' + t('shared_edit_scope');
    q(".enqueue-button").textContent = t(state.editing === "defaults" ? "save_defaults" : state.editing ? "save_task" : "workbench.enqueue_only");
    q(".enqueue-button").disabled = state.busy || state.selecting || (!state.editing && !selectedCount());
    q(".download-button").disabled = state.busy || state.selecting || !selectedCount();
    q(".download-button").hidden = !!state.editing;
    q(".grouping-field").hidden = !!state.editing || !sources.some(source => source.kind !== "media");
    cover(q(".preview-cover"), media);
    q(".audio-treatment").hidden = value.kind !== "audio" || value.format !== "m4a";
    m4aField.hidden = q('.audio-treatment').hidden;
    qualityHelp.textContent = t(value.processing.formatIds?.length ? 'polish.manual_quality_help' : value.kind === 'video' ? 'polish.video_quality_help' : 'polish.audio_quality_help');
    q('.quality-value').textContent = t(value.processing.formatIds?.length ? 'polish.selected_streams' : 'polish.audio_fixed');
    outputProcessingNote.textContent = t('polish.processing_' + (value.processing.formatIds?.length ? 'manual' : value.format === 'm4a' ? 'm4a_' + value.processing.m4aMode : value.kind === 'video' ? value.format === 'mp4' ? 'mp4' : value.format === 'webm' || value.format === 'mkv' ? 'video_container' : 'original' : value.format === 'none' ? 'original' : 'audio')).replace('{format}', value.format.toUpperCase());
    q(".current-filename").hidden = !state.tasks.has(state.editing);
    const single = state.tasks.has(state.editing) || !state.editing && sources.length === 1 && sources[0].kind === "media";
    all('.advanced-options [data-panel]').forEach(button => button.disabled = !single);
    all(".single-video-hint").forEach(element => element.hidden = single);
    q(".subtitle-summary").textContent = q(".subtitle-select").selectedOptions[0]?.textContent || "";
    q(".format-summary").textContent = value.processing.formatIds?.join(" + ") || t("automatic");
    q(".processing-summary").textContent = q(".retry-select").selectedOptions[0]?.textContent + " · " + q(".concurrency-select").selectedOptions[0]?.textContent;
    timingSummary.textContent = t(value.processing.sections.length ? 'polish.sections_count' : 'polish.no_sections').replace('{count}', value.processing.sections.length);
    q('#polish-help-keyframes_help').textContent = t(value.processing.sections.length ? 'polish.keyframes_help' : 'polish.keyframes_without_sections');
    q(".files-summary").textContent = value.processing.filename || t(value.processing.keepFiles ? "keep_files" : "backend_defaults");
    q(".raw-summary").textContent = t(value.processing.rawEnabled && value.processing.rawArgs ? "raw_enabled" : "raw_disabled");
    q('.preview-quality').textContent = value.kind === 'video' ? q('.quality-select').selectedOptions[0]?.textContent || '' : q('.quality-value').textContent;
    q('.preview-scope').textContent = state.editing === 'defaults' ? q('.default-output-scope').textContent : q('.output-count').textContent;
    for (const key of ['quality', 'scope']) { q('.preview-' + key).title = q('.preview-' + key).textContent; q('.preview-full-' + key).value = q('.preview-' + key).textContent; }
    q('.scope-note').textContent = t('context.draft_note'); q('.scope-note').title = q('.scope-note').textContent;
    renderContext();
    previewArguments(value);
  }
  function markEdited(key) { if (!processingDraft.edited.includes(key)) processingDraft.edited.push(key); }
  async function previewArguments(value) {
    const revision = ++previewRevision;
    previewValidation.textContent = t('context.checking'); previewValidation.setAttribute('role', 'status'); previewArgumentsText.textContent = ''; q('.preview-full-arguments').value = ''; q('.preview-full-validation').value = previewValidation.textContent;
    try {
      const result = await bridge.request("previewOutput", {options: value});
      if (revision !== previewRevision) return;
      previewValidation.textContent = t('context.valid'); previewValidation.title = previewValidation.textContent; q('.preview-full-validation').value = previewValidation.textContent;
      previewArgumentsText.textContent = result.arguments; previewArgumentsText.title = result.arguments;
      q(".command-preview").value = result.arguments;
      q(".filename-example").textContent = result.filename;
      pathValue(q('.filename-example'), result.filename, true);
      q('.preview-full-filename').value = result.filename; q('.preview-full-arguments').value = result.arguments;
      q(".raw-format-state").hidden = !result.rawFormat;
      if (result.rawFormat) outputProcessingNote.textContent = t('polish.processing_raw');
      advancedInherited.hidden = !value.processing.rawEnabled || !value.processing.rawArgs.trim();
      for (const [selector, group, key] of [['.retry-select', 2, 'retries_help'], ['.concurrency-select', 3, 'fragments_help']]) { const inherited = q(selector).value === 'keep' && result.originalGroups[group]; q('#polish-help-' + key).textContent = (inherited ? t('keep_existing_arguments') + ' · ' : '') + t('polish.' + key); }
      if (value.kind === 'audio') { const quality = t(result.rawFormat ? 'keep_existing_arguments' : value.processing.formatIds?.length ? 'polish.selected_streams' : 'polish.audio_fixed'); q('.quality-value').textContent = quality; q('.preview-quality').textContent = quality; q('.preview-quality').title = quality; q('.preview-full-quality').value = quality; }
      q(".argument-summary").textContent = t("core_options_preview");
      for (const [key, checked] of Object.entries(result.flags)) {
        const element = key === "cover" ? q(".cover-check") : key === "metadata" ? q(".metadata-check") : q('[data-flag="' + key + '"]');
        if (element) element.checked = checked;
      }
    } catch (error) { if (revision === previewRevision) { previewValidation.textContent = error.message; previewValidation.title = error.message; previewValidation.setAttribute('role', 'alert'); q('.preview-full-validation').value = error.message; q('.preview-full-arguments').value = ''; if (state.view === 'output') report(error); } }
  }
  function renderSource() {
    const sources = selectionSources();
    const list = q('.candidate-list'), identity = sources.map(source => source.sourceId).join(','), scrollTop = list.dataset.sources === identity ? sourceScroll.scrollTop : 0;
    list.dataset.sources = identity; list.replaceChildren();
    q(".source-heading").hidden = !state.media;
    q(".source-count").textContent = state.source ? t("available_media").replace("{count}", sources.reduce((count, source) => count + source.availableCount, 0)) : "";
    q(".selected-count").textContent = selectedCount() + " / " + sources.reduce((count, source) => count + source.entries.length, 0);
    q(".selection-hint").textContent = selectedCount() ? t("selected_media").replace("{count}", selectedCount()) : t("select_media");
    q('.grouping-field').hidden = !!state.collectionEditing || !sources.some(source => source.kind !== 'media');
    q(".next-output").disabled = !selectedCount() || state.parsing || state.selecting;
    q(".next-output").textContent = t(state.collectionEditing ? "confirm_selection" : "next_output");
    quickStart.hidden = !!state.collectionEditing; quickStart.disabled = q('.next-output').disabled;
    quickStart.title = state.settings ? state.settings.newTaskOptions.format.toUpperCase() + ' · ' + state.settings.newTaskOptions.outputFolder : '';
    selectionNote.hidden = !state.collectionEditing;
    if (state.collectionEditing) {
      const before = state.collections.get(state.collectionEditing)?.source.selectedEntries || [], after = state.source.selectedEntries;
      selectionNote.textContent = t('workbench.reselect_diff').replace('{keep}', before.filter(id => after.includes(id)).length).replace('{add}', after.filter(id => !before.includes(id)).length).replace('{remove}', before.filter(id => !after.includes(id)).length);
    }
    q(".analyze-link").disabled = state.parsing;
    pasteSource.disabled = state.parsing || state.busy;
    cancelSource.textContent = t('source_cancel'); cancelSource.hidden = !state.parsing; cancelSource.disabled = !state.parsing;
    reparseSources.textContent = t('source_reparse'); reparseSources.disabled = !sources.length || state.parsing || state.selecting; reparseSources.hidden = !!state.collectionEditing;
    sourceFocusName.textContent = t('source_current'); sourceFocusLabel.hidden = sources.length < 2; sourceFocus.setAttribute('aria-label', sourceFocusName.textContent);
    sourceFocus.replaceChildren(...sources.map(source => new Option(source.metadata.title, source.sourceId))); sourceFocus.value = state.source?.sourceId || ''; sourceFocus.disabled = state.parsing || state.selecting;
    const currentChannel = state.source?.kind === 'channel' || state.channels.has(state.source?.sourceId);
    q(".channel-tab").closest("label").hidden = !currentChannel || !!state.collectionEditing; q('.channel-tab').disabled = state.parsing || state.selecting;
    q('.channel-tab').closest('.selection-tools').hidden = !currentChannel && sources.length < 2;
    channelPickers.replaceChildren();
    if (!state.collectionEditing) for (const source of sources) {
      const channel = state.channels.get(source.sourceId); if (!channel) continue;
      const label = document.createElement("label"), select = document.createElement("select");
      label.textContent = t("channel_playlists") + (sources.length > 1 ? " · " + channel.root.metadata.title : "");
      select.className = "source-picker"; select.dataset.source = source.sourceId; select.setAttribute("aria-label", label.textContent);
      select.append(new Option(channel.root.entries.length ? channel.root.metadata.title : t("channel_playlists"), channel.root.entries.length ? channel.root.url : ""),
        ...channel.playlists.map(playlist => new Option(playlist.title, playlist.url)));
      select.value = source.url; if (select.selectedIndex < 0) select.selectedIndex = 0;
      select.disabled = state.parsing || state.selecting || channel.loading || !channel.playlists.length && !channel.root.entries.length;
      select.title = channel.loading ? t("parsing") : channel.error?.message || (!channel.playlists.length ? t("no_channel_playlists") : "");
      select.addEventListener("change", () => { if (select.value) replaceSource(source, select.value, select.value === channel.root.url ? channel.root.category : ""); });
      label.append(select); channelPickers.append(label);
    }
    channelPickers.hidden = !channelPickers.childElementCount;
    inputFeedback.hidden = !!state.collectionEditing || !state.inputResults.length;
    inputFeedback.firstElementChild.textContent = state.inputResults.map(result => t("input." + result.status)).join(" · ");
    inputFeedback.firstElementChild.title = inputFeedback.firstElementChild.textContent;
    inputFeedback.lastElementChild.replaceChildren(...state.inputResults.map(result => {
      const row = document.createElement('p'); row.dataset.inputStatus = result.status; row.textContent = t('input.' + result.status) + ' · ' + result.input + (result.error ? ' · ' + (result.error.message || t('parse_failed')) : '');
      if (result.status === 'failed' || result.error && result.status === 'ready') { const retry = document.createElement('button'); retry.type = 'button'; retry.className = 'text-button retry-source cursor-interaction'; retry.textContent = t('source_retry'); retry.disabled = state.parsing; retry.addEventListener('click', () => retrySource(result)); row.append(retry); }
      return row;
    }));
    if (!state.source) { renderContext(); return; }
    const media = state.source.metadata, fragment = document.createDocumentFragment(); let position = 0;
    for (const source of sources) for (const entry of source.entries) {
      const index = sources.length > 1 ? ++position : entry.index;
      if (sourceAvailability.value === 'available' && !entry.available || sourceAvailability.value === 'unavailable' && entry.available) continue;
      const row = candidateTemplate.cloneNode(true), input = row.querySelector("input");
      row.dataset.source = source.sourceId; row.dataset.entry = entry.entryId;
      row.querySelector("strong").textContent = entry.media.title || t("entry_unavailable");
      row.querySelector('strong').title = row.querySelector('strong').textContent;
      row.querySelector(".video-info > span").textContent = entry.error?.message ||
        (entry.media.uploader || entry.media.channel || t("unknown_author")) + " · " + duration(entry.media.duration);
      row.querySelector('.video-info > span').title = row.querySelector('.video-info > span').textContent;
      row.querySelector(".video-index").textContent = String(index).padStart(2, "0");
      input.checked = entry.selected; input.disabled = !entry.available || state.selecting || state.parsing;
      input.setAttribute("aria-label", entry.media.title || t("entry_unavailable"));
      input.addEventListener("change", () => changeSelection(ids => input.checked ? [...ids, entry.entryId] : ids.filter(id => id !== entry.entryId), source));
      row.classList.toggle("is-selected", entry.selected); cover(row.querySelector(".cover"), entry.media); fragment.append(row);
    }
    q(".candidate-list").append(fragment);
    sourceScroll.scrollTop = scrollTop;
    q(".source-heading h2").textContent = media.title;
    q('.source-heading h2').title = q('.source-heading h2').textContent;
    q(".source-heading p").textContent = t("source." + state.source.kind) + " · " + (media.webpage_url_domain || new URL(state.source.url).hostname);
    q(".source-avatar").textContent = (media.uploader || media.title).slice(0, 1);
    sourceKind.textContent = t(sources.length > 1 ? "multiple_sources" : "source." + state.source.kind);
    if (currentChannel) q(".channel-tab").value = state.source.category || state.channels.get(state.source.sourceId)?.root.category || 'videos';
    renderContext();
  }
  function changeSelection(update, source = state.source) {
    const sourceId = source?.sourceId; if (!sourceId) return;
    ++selectionPending; state.selecting = true;
    q(".next-output").disabled = true;
    all(".candidate input, .source-picker").forEach(control => control.disabled = true);
    selectionChain = selectionChain.then(async () => {
      const current = state.collectionEditing ? state.source : state.sources.get(sourceId) || (state.source?.sourceId === sourceId ? state.source : null); if (!current) return;
      const ids = update(current.selectedEntries, current.entries);
      if (state.collectionEditing) {
        const selected = new Set(ids); state.source.selectedEntries = ids;
        for (const entry of state.source.entries) entry.selected = selected.has(entry.entryId);
      } else {
        const result = await bridge.request("selectSource", {sourceId, entryIds: ids}); state.sources.set(sourceId, result);
        if (state.source?.sourceId === sourceId) state.source = result;
      }
    }).catch(report).finally(() => { state.selecting = --selectionPending > 0; state.selected = !!selectedCount(); renderSource(); });
  }
  const summary = value => value?.summary || value;
  const mediaOf = value => value?.source?.metadata || value?.media;
  const displayFields = {format: '[data-column="format"]', note: '[data-column="note"]', ext: '[data-column="ext"]', size: '[data-column="size"]', siteIcon: '.site-icon', siteText: '.site-text'};
  function patchDisplay(row, value) {
    const display = value.display, preferences = state.settings.application.appearance.queueDisplay;
    const meta = row.querySelector('.job-meta'), parts = [];
    if (value.collectionId) parts.push(t('collection_count').replace('{count}', value.summary.total));
    const site = row.querySelector('.job-site'), icon = site.querySelector('.job-site-icon'), name = site.querySelector('.job-site-name');
    icon.hidden = !preferences.siteIcon; name.hidden = !preferences.siteText;
    name.style.marginLeft = preferences.siteIcon ? '4px' : '';
    name.textContent = display.siteName || '---';
    if (icon.dataset.url !== display.siteIcon) {
      icon.dataset.url = display.siteIcon; icon.replaceChildren();
      const placeholder = lucide.createElement(lucide.icons.Globe, {width: 16, height: 16, 'aria-hidden': 'true'});
      placeholder.style.verticalAlign = 'middle'; icon.append(placeholder);
      if (display.siteIcon) {
        const image = new Image(); image.alt = ''; image.draggable = false; image.width = image.height = 16; image.style.verticalAlign = 'middle'; image.hidden = true;
        image.addEventListener('load', () => { placeholder.hidden = true; placeholder.style.display = 'none'; image.hidden = false; }, {once: true});
        image.addEventListener('error', () => image.remove(), {once: true}); image.src = display.siteIcon; icon.append(image);
      }
    }
    site.hidden = !preferences.siteIcon && !preferences.siteText; site.title = display.siteName || t('unknown_site');
    parts.push(site);
    for (const key of ['format', 'note', 'ext', 'size']) {
      const field = row.querySelector('.job-display-' + key); field.hidden = !preferences[key];
      field.textContent = key === 'size' ? display.size == null ? '---' : (display.approximate ? '~' : '') + sizeText(display.size) : display[key] || '---';
      field.title = q(displayFields[key]).closest('label').textContent.trim();
      parts.push(field);
    }
    if (value.options.cover) parts.push(t('embed_cover'));
    let visible = 0;
    meta.replaceChildren(...parts.flatMap(part => typeof part !== 'string' && part.hidden ? [part] : visible++ ? [' · ', part] : [part])); meta.hidden = !visible;
  }
  function renderQueueDisplay() {
    const preferences = state.settings.application.appearance.queueDisplay, diagnostics = state.settings.diagnostics;
    for (const [key, selector] of Object.entries(displayFields)) { q(selector).checked = preferences[key]; q(selector).disabled = diagnostics.enabled && !diagnostics.writable; }
    for (const [id, row] of state.rows) patchDisplay(row, model(id));
  }
  function patchJob(row, value) {
    if (!value) return;
    const task = summary(value), group = !!value.collectionId, media = mediaOf(value);
    row.dataset.status = task.status; row.dataset.execution = group ? "collection" : task.executionId;
    row.dataset.type = group ? "collection" : "media";
    row.classList.toggle("is-selected", state.picked.has(row.dataset.job) || row.dataset.job === state.selectedTask);
    row.setAttribute("aria-selected", String(state.picked.has(row.dataset.job)));
    row.querySelector(".job-pick").checked = state.picked.has(row.dataset.job);
    row.querySelector(".job-open").textContent = !group && state.settings.application.applicationBehavior.displayCustomFilenames && value.options.processing.filename ? value.options.processing.filename.split(/[\\/]/).at(-1) : media.title;
    row.querySelector('.job-open').title = row.querySelector('.job-open').textContent;
    patchDisplay(row, value);
    const counts = group ? t("collection_counts").replace("{done}", task.completed).replace("{failed}", task.failed).replace("{skipped}", task.skipped) : "";
    row.querySelector(".job-extra").textContent = group ? counts + (task.currentTitle ? " · " + task.currentIndex + "/" + task.total + " · " + task.currentTitle : "") : value.options.outputFolder;
    if (!group) pathValue(row.querySelector('.job-extra'), value.options.outputFolder);
    const status = row.querySelector(".job-state"); status.className = "job-state " + task.status;
    status.firstChild.textContent = statusText(task.status);
    status.querySelector(".job-speed").textContent = group ? task.speed || "" : task.text || "";
    const progress = row.querySelector(".download-progress");
    progress.hidden = !["running", "processing", "stopping"].includes(task.status);
    progress.setAttribute("aria-valuenow", String(task.percent || 0));
    progress.firstElementChild.style.width = (task.percent || 0) + "%";
    const action = row.querySelector(".job-action"), actions = task.actions;
    action.textContent = t(actions.stop ? "stop" : actions.retry ? "retry" : actions.start ? "start" : actions.open ? "open_file" : "start");
    action.disabled = !(actions.stop || actions.open || actions.retry || actions.start);
  }
  function renderInspector() {
    for (const id of state.logs.keys()) if (id !== sidebarTaskId() && !(state.view === 'detail' && panel?.kind === 'logs' && panel.mediaId === id)) state.logs.delete(id);
    q('.copy-options').disabled = scopePending || !model(state.selectedTask);
    q('.copy-options').title = model(state.selectedTask) ? '' : t('select_task_first');
    const id = sidebarTaskId(), value = model(id), task = summary(value), media = mediaOf(value);
    q('.task-inspector').hidden = !value || !['queue', 'detail'].includes(state.view);
    if (!value) { renderContext(); return; }
    q(".detail-title").textContent = media.title;
    q('.detail-title').title = media.title;
    q(".detail-status").textContent = statusText(task.status) + (task.percent ? " · " + task.percent.toFixed(1) + "%" : "");
    q('.detail-status').dataset.status = task.status;
    q(".detail-source").textContent = value.source?.url || value.url;
    q('.detail-source').title = q('.detail-source').textContent; q('.detail-source').classList.add('truncate-end');
    q(".detail-output").textContent = value.options.format.toUpperCase();
    q(".detail-naming").textContent = t("naming." + value.options.naming);
    q('.detail-output').classList.add('truncate-end'); q('.detail-output').title = q('.detail-output').textContent;
    q('.detail-naming').classList.add('truncate-end'); q('.detail-naming').title = q('.detail-naming').textContent;
    q('.detail-naming').classList.add('inspector-secondary'); q('.detail-naming').previousElementSibling.classList.add('inspector-secondary');
    pathValue(q('.task-inspector dl').lastElementChild, task.file || value.options.processing.filename || value.options.outputFolder, !!(task.file || value.options.processing.filename));
    detailError.hidden = !task.error?.message; detailError.textContent = task.error?.message || ''; detailError.title = detailError.textContent;
    const cachedLog = state.logs.get(id), log = cachedLog?.executionId === task.executionId ? cachedLog : null;
    const recent = value.collectionId ? t('collection_counts').replace('{done}', task.completed).replace('{failed}', task.failed).replace('{skipped}', task.skipped) +
      (task.currentTitle ? '\n' + task.currentIndex + '/' + task.total + ' · ' + task.currentTitle : '') : log?.text || task.text || task.file || statusText(task.status);
    q('.log-content').replaceChildren(...recent.trimEnd().split(/\r?\n/).slice(-3).map(line => { const span = document.createElement('span'); span.textContent = line; span.title = line; return span; }));
    if (!value.collectionId && (!log || log.executionId !== task.executionId)) {
      state.logs.set(id, {executionId: task.executionId, revision: -1, text: ''});
      loadLog(id).catch(report);
    }
    q(".edit-job").disabled = value.collectionId ? false : !task.actions.edit && !(state.settings.taskOptionsScope.mode === 'shared' && ['running', 'processing', 'stopping'].includes(task.status) && !task.refreshing);
    q('.edit-job').textContent = t(value.collectionId ? 'workbench.edit_collection_defaults' : 'workbench.edit_task');
    q(".reselect-job").disabled = !value.collectionId || !task.actions.select;
    q('.reselect-job').title = value.collectionId && !task.actions.select ? t('workbench.reselect_locked') : '';
    q('.job-menu [data-task="split"]').disabled = !value.collectionId || !task.actions.split;
    const skip = q('.job-menu [data-task="skip"]');
    skip.disabled = value.collectionId ? !task.actions.select : !task.actions.edit;
    skip.textContent = t(task.status === "skipped" ? "restore_downloadable" : "skip_download");
    q('.job-menu [data-task="file"]').disabled = !task.actions.open;
    q('.job-menu [data-task="folder"]').disabled = !!value.collectionId;
    q('.job-menu [data-task="refresh"]').disabled = value.collectionId ? !task.actions.remove : !task.actions.refresh;
    all('.job-menu [data-panel]').forEach(button => button.disabled = !!value.collectionId || (button.dataset.panel === "json" ? !task.actions.details : !task.actions.edit));
    q('.task-inspector [data-panel="logs"]').disabled = !!value.collectionId;
    const siblings = groupOf(state.selectedTask) ? state.groups.get(groupOf(state.selectedTask))?.items || [] : state.ungrouped.items;
    const position = siblings.indexOf(state.selectedTask);
    q('.job-menu [data-task="up"]').disabled = position <= 0;
    q('.job-menu [data-task="down"]').disabled = position < 0 || position === siblings.length - 1;
    q('.job-menu [data-group-action="assignTask"]').disabled = !!value.parentId;
    taskContextAction.textContent = t(task.actions.stop ? 'stop' : task.actions.retry ? 'retry' : task.actions.start ? 'start' : 'open_file');
    taskContextAction.disabled = !(task.actions.stop || task.actions.retry || task.actions.start || task.actions.open);
    taskContextCopy.textContent = t('copy_url'); renderContext();
  }
  const groupOf = id => state.itemGroups.get(state.tasks.get(id)?.parentId || id) || '';
  const currentGroup = () => state.currentGroup === '' ? state.ungrouped : state.groups.get(state.currentGroup);
  const groupNameText = group => group?.name || t('ungrouped');
  const groupCounts = group => t('group_counts').replace('{items}', group?.items.length || 0).replace('{done}', group?.summary.completed || 0).replace('{failed}', group?.summary.failed || 0).replace('{stopped}', group?.summary.stopped || 0).replace('{skipped}', group?.summary.skipped || 0);
  const scopeItems = scope => scope === 'all' ? state.items : scope === 'group' ? currentGroup()?.items || [] : [...state.picked];
  function expandedTasks(ids, tasks = state.tasks, collections = state.collections) {
    const expanded = new Set(); for (const id of ids) for (const taskId of collections.get(id)?.children || [id]) expanded.add(taskId);
    return [...expanded].map(id => tasks.get(id)).filter(Boolean);
  }
  function scopeEligible(task, action, scope, collections = state.collections) {
    if (action === 'start') return task.actions.start || task.actions.retry;
    if (action === 'stop') return task.actions.stop || scope !== 'all' && task.actions.start;
    if (action === 'retry') return task.actions.retry;
    if (action === 'clearCompleted') return task.status === 'done' && (!task.parentId || collections.get(task.parentId)?.summary.status === 'done');
    return task.actions.remove;
  }
  const taskReason = (task, action, collections = state.collections) => t(task.refreshing ? 'polish.reason_refreshing' : action === 'clearCompleted' && task.status === 'done' && task.parentId && collections.get(task.parentId)?.summary.status !== 'done' ? 'polish.reason_collection' : 'polish.reason_state').replace('{status}', statusText(task.status));
  function renderSelectionActions() {
    const tasks = expandedTasks([...state.picked]), blocked = tasks.filter(task => task.refreshing || !task.actions.remove);
    for (const action of ['start', 'stop']) q('.batch-bar [data-batch="' + action + '"]').disabled = !tasks.some(task => scopeEligible(task, action, 'selection')) || tasks.some(task => task.refreshing);
    q('.batch-bar [data-batch="remove"]').disabled = !tasks.length || !!blocked.length;
    q('.batch-bar [data-batch="remove"]').title = blocked.map(task => task.media.title + ' · ' + taskReason(task, 'remove')).join('\n');
    const canEdit = tasks.length && tasks.every(task => task.actions.edit && !task.refreshing);
    availabilityOpen.disabled = !canEdit; availabilityOpen.title = canEdit ? '' : t('polish.edit_unavailable');
    q('.batch-more [data-batch="refresh"]').disabled = !tasks.length || tasks.some(task => !task.actions.refresh);
    const assign = q('.batch-more [data-group-action="assignSelection"]'), child = [...state.picked].some(id => model(id)?.parentId);
    assign.disabled = !state.picked.size || child; assign.title = child ? t('polish.group_parent_only') : '';
    selectionFeedback.hidden = !blocked.length; selectionFeedback.textContent = t('polish.selection_blocked').replace('{count}', blocked.length); selectionFeedback.title = q('.batch-bar [data-batch="remove"]').title;
  }
  function renderGroupContext() {
    const group = currentGroup(), scope = q('.queue-scope').value;
    scopeTools.dataset.scope = scope;
    groupContext.hidden = state.view !== 'queue';
    groupContextName.textContent = scope === 'all' ? t('scope_all') : group ? groupNameText(group) : t('polish.select_group');
    const ids = scopeItems(scope), tasks = expandedTasks(ids), unavailable = scope === 'group' && !group;
    groupContextState.textContent = t('polish.scope_count').replace('{items}', ids.length).replace('{tasks}', tasks.length);
    for (const element of groupContext.children) element.title = element.textContent;
    scopeTools.hidden = !state.items.length; q('.queue-select-row').hidden = !state.items.length;
    for (const button of all('.queue-scope-tools [data-batch]')) {
      const action = button.dataset.batch === 'clearDone' ? 'clearCompleted' : button.dataset.batch, blocked = tasks.filter(task => task.refreshing || action === 'remove' && !task.actions.remove);
      button.disabled = unavailable || !tasks.some(task => scopeEligible(task, action, scope)) || !!blocked.length;
      button.title = blocked.map(task => task.media.title + ' · ' + taskReason(task, action)).join('\n') || (unavailable ? t('polish.select_group') : action === 'start' ? t('polish.start_hint') : action === 'stop' && scope === 'group' ? t('polish.stop_group_hint') : t('scope_' + scope));
      const label = t('polish.' + (action === 'start' ? scope === 'all' ? 'start_queue' : 'start_group' : action === 'stop' ? 'stop_scope' : action === 'remove' ? 'remove_scope' : 'clear_scope'));
      if (button.parentElement === scopeTools) { button.querySelector('.scope-full-label').textContent = label; button.setAttribute('aria-label', t('polish.' + (action === 'start' ? 'start_compact' : 'clear_compact')) + ' · ' + label); }
      else button.textContent = label;
    }
    q('.queue-operations > summary').setAttribute('aria-label', t('polish.range_more'));
    q('.queue-operations > summary').title = t('polish.range_more');
    q('.queue-operations [data-group-action="manage"]').disabled = !group?.groupId;
  }
  function openGroupMenu(mode, anchor, id = state.currentGroup) {
    closeMenu(); groupEditing = {mode, id, items: mode === 'assignTask' ? [state.selectedTask] : [...state.picked], anchor, jobId: anchor.closest('.job')?.dataset.job, groupId: anchor.closest('.queue-group')?.dataset.queueGroup};
    const group = id === '' ? state.ungrouped : state.groups.get(id);
    groupMenuTitle.textContent = t(mode === 'create' ? 'group_create' : mode.startsWith('assign') ? 'group_assign' : mode === 'rename' ? 'group_rename' : 'group_manage') + (group ? ' · ' + groupNameText(group) : '');
    groupMenuTitle.title = groupMenuTitle.textContent;
    groupName.hidden = !['create', 'rename'].includes(mode); groupName.value = mode === 'rename' ? group.name : ''; groupName.setAttribute('aria-label', t('group_name'));
    groupTarget.hidden = !mode.startsWith('assign'); groupTarget.setAttribute('aria-label', t('group_target'));
    groupTarget.replaceChildren(new Option(t('ungrouped'), ''), ...[...state.groups.values()].map(group => new Option(group.name, group.groupId)));
    groupTarget.value = id || '';
    groupMessage.textContent = mode === 'manage' ? groupCounts(group) + ' · ' + t('group_delete_note') : mode.startsWith('assign') ? t('group_assign_note') : t('group_name_note');
    groupMessage.setAttribute('role', 'status');
    for (const button of groupMenuActions.children) {
      const action = button.dataset.groupAction, manage = mode === 'manage';
      button.hidden = action === 'cancel' ? false : action === 'submit' ? manage : !manage;
      const index = [...state.groups.keys()].indexOf(id);
      button.disabled = manage && (['rename', 'delete', 'up', 'down'].includes(action) && !group?.groupId || action === 'up' && index <= 0 || action === 'down' && index === state.groups.size - 1 || action === 'remove' && !group?.summary.actions?.remove);
    }
    if (!groupMenu.open) groupMenu.showModal(); if (!groupName.hidden) groupName.focus(); else if (!groupTarget.hidden) groupTarget.focus(); else groupMenuActions.querySelector('[data-group-action="cancel"]').focus();
  }
  function closeGroupMenu() {
    const origin = groupEditing, anchor = origin?.jobId ? state.rows.get(origin.jobId)?.querySelector('.job-more') : origin?.groupId !== undefined ? state.groupRows.get(origin.groupId)?.section.querySelector('.queue-group-more') : origin?.anchor;
    groupMenu.close(); groupEditing = null; (anchor?.isConnected ? anchor : q('.group-create')).focus();
  }
  groupMenu.addEventListener('cancel', event => { event.preventDefault(); closeGroupMenu(); });
  async function applyScope(action, scope, groupId = state.currentGroup, items = [...state.picked]) {
    const current = await bridge.request('listQueue'), tasks = new Map(current.tasks.map(task => [task.taskId, task])), collections = new Map(current.collections.map(value => [value.collectionId, value]));
    const selected = scope === 'all' ? current.items : scope === 'group' ? (groupId ? current.groups.find(group => group.groupId === groupId) : current.ungrouped)?.items || [] : items;
    const expanded = expandedTasks(selected, tasks, collections), blocked = expanded.filter(task => task.refreshing || action === 'remove' && !task.actions.remove), ignored = expanded.filter(task => !scopeEligible(task, action, scope, collections));
    if (['remove', 'clearCompleted'].includes(action) || blocked.length || ignored.length) {
      const scopeName = t('scope_' + scope) + (scope === 'group' ? ' · ' + (current.groups.find(group => group.groupId === groupId)?.name || t('ungrouped')) : '');
      const text = t('polish.operation_scope').replace('{action}', t(action === 'clearCompleted' ? 'polish.clear_scope' : action)).replace('{scope}', scopeName) + '\n' +
        t('polish.preview_count').replace('{total}', expanded.length).replace('{affected}', expanded.length - ignored.length).replace('{ignored}', ignored.length) + '\n' +
        expanded.map(task => task.media.title + ' · ' + (blocked.includes(task) || ignored.includes(task) ? taskReason(task, action, collections) : statusText(task.status))).join('\n') + '\n' + t(blocked.length ? 'polish.preview_blocked' : 'polish.preview_core');
      if (!await confirmScope(text, !!blocked.length || !expanded.length)) return null;
    }
    const result = await bridge.request('operateQueueScope', {action, scope, groupId: groupId || '', itemIds: items});
    say(t('scope_result').replace('{scope}', t('scope_' + scope) + (scope === 'group' ? ' · ' + groupNameText(groupId ? state.groups.get(groupId) : state.ungrouped) : '')).replace('{affected}', result.affectedTaskIds.length).replace('{ignored}', result.ignoredTaskIds.length));
    return result;
  }
  async function runItemAction(id) {
    const value = model(id); if (!value) return;
    const task = summary(value);
    if (!task.actions.stop && !task.actions.retry && !task.actions.start && task.actions.open) return bridge.request('openFile', {taskId: id});
    return applyScope(task.actions.stop ? 'stop' : task.actions.retry ? 'retry' : 'start', 'selection', null, [id]);
  }
  for (const button of all('[data-group-action]')) button.addEventListener('click', async event => {
    event.stopPropagation(); const action = button.dataset.groupAction;
    if (['create', 'assignSelection', 'assignTask', 'manage'].includes(action) && !groupMenu.contains(button)) {
      const anchor = button.matches('.group-create') ? button : button.closest('details')?.querySelector('summary') || state.rows.get(state.selectedTask)?.querySelector('.job-more') || button;
      if (anchor) openGroupMenu(action, anchor); return;
    }
    if (action === 'cancel') { closeGroupMenu(); return; }
    const draft = groupEditing, group = draft?.id === '' ? state.ungrouped : state.groups.get(draft?.id);
    if (!draft) return;
    if (action === 'rename') { openGroupMenu('rename', draft.anchor, draft.id); return; }
    button.disabled = true;
    try {
      if (action === 'submit') {
        if (draft.mode === 'create') { const result = await bridge.request('createQueueGroup', {name: groupName.value}); state.currentGroup = result.groupId; }
        else if (draft.mode === 'rename') await bridge.request('updateQueueGroup', {groupId: group.groupId, name: groupName.value, collapsed: group.collapsed, position: [...state.groups.keys()].indexOf(group.groupId)});
        else await bridge.request('assignQueueGroup', {itemIds: draft.items, groupId: groupTarget.value});
      } else if (action === 'delete') { if (!await confirmScope(t('polish.delete_group').replace('{name}', group.name).replace('{count}', group.items.length))) return; await bridge.request('deleteQueueGroup', {groupId: draft.id}); }
      else if (action === 'up' || action === 'down') await bridge.request('updateQueueGroup', {groupId: draft.id, name: group.name, collapsed: group.collapsed, position: [...state.groups.keys()].indexOf(draft.id) + (action === 'up' ? -1 : 1)});
      else if (!await applyScope(action, 'group', draft.id)) return;
      closeGroupMenu(); renderGroupContext(); if (!['start', 'stop', 'retry', 'remove'].includes(action)) say(t('group_saved'));
    } catch (error) { groupMessage.textContent = error.message; groupMessage.setAttribute('role', 'alert'); report(error); }
    finally { button.disabled = false; }
  });
  groupName.addEventListener('keydown', event => { if (event.key === 'Enter') { event.preventDefault(); groupMenu.querySelector('[data-group-action="submit"]').click(); } });
  function groupSection(group) {
    const section = document.createElement('section'), heading = document.createElement('div'), toggle = document.createElement('button'), name = document.createElement('button'), status = document.createElement('span'), more = document.createElement('button'), list = document.createElement('div');
    section.className = 'queue-group'; section.dataset.queueGroup = group.groupId; heading.className = 'queue-group-heading'; list.className = 'queue-group-items';
    toggle.type = name.type = more.type = 'button'; toggle.className = name.className = more.className = 'text-button cursor-interaction';
    toggle.classList.add('queue-group-toggle'); toggle.textContent = group.collapsed ? '›' : '⌄'; toggle.hidden = !group.groupId; toggle.title = t(group.collapsed ? 'group_expand' : 'group_collapse'); toggle.setAttribute('aria-label', toggle.title); toggle.setAttribute('aria-expanded', String(!group.collapsed));
    name.textContent = groupNameText(group); name.title = name.textContent; name.classList.add('queue-group-title'); status.className = 'queue-group-status truncate-end'; more.textContent = t('more'); more.classList.add('queue-group-more'); more.setAttribute('aria-label', t('group_manage') + ' · ' + name.textContent);
    name.addEventListener('click', () => { state.currentGroup = group.groupId; renderGroupContext(); });
    more.addEventListener('click', () => { state.currentGroup = group.groupId; renderGroupContext(); openGroupMenu('manage', more, group.groupId); });
    toggle.addEventListener('click', () => bridge.request('updateQueueGroup', {groupId: group.groupId, name: group.name, collapsed: !group.collapsed, position: [...state.groups.keys()].indexOf(group.groupId)}).catch(report));
    heading.addEventListener('dragover', event => { if (state.dragId) event.preventDefault(); });
    heading.addEventListener('drop', event => { if (!state.dragId || event.dataTransfer.getData('application/x-ytdlp-queue-id') !== state.dragId) return; event.preventDefault(); const id = state.dragId; state.dragId = null; bridge.request('assignQueueGroup', {itemIds: [id], groupId: group.groupId}).catch(report); });
    heading.append(toggle, name, status, more); section.append(heading, list); state.groupRows.set(group.groupId, {section, list, status}); patchGroupHeader(group);
    return section;
  }
  function patchGroupHeader(group) {
    const row = state.groupRows.get(group.groupId); if (!row) return;
    row.status.textContent = group.items.length + ' · ' + statusText(group.summary.status || 'waiting') + ' · ' + (group.summary.percent || 0).toFixed(1) + '%'; row.status.title = groupCounts(group); row.list.hidden = !!group.collapsed;
  }
  function visibleJob(id, value) {
    const filter = product.querySelector('.queue-filter[aria-pressed="true"]')?.dataset.filter || "all", status = summary(value).status;
    const groupFilter = q('.group-filter').value, owner = groupOf(id);
    const matches = value => { const status = summary(value).status; return (filter !== 'needs' || ['failed', 'stopped', 'partial'].includes(status)) && (!queueSearch.value.trim() || mediaOf(value).title.toLocaleLowerCase().includes(queueSearch.value.trim().toLocaleLowerCase())); };
    if (!matches(value) && !(value.children || []).some(child => model(child) && matches(model(child)))) return false;
    const rootId = value.parentId || id;
    return !(filter === 'active' && ['done', 'failed', 'stopped', 'partial', 'skipped'].includes(status) || filter === 'done' && status !== 'done' ||
      state.groups.get(owner)?.collapsed || groupFilter === 'single' && value.collectionId || groupFilter === 'ungrouped' && owner || state.groups.has(groupFilter) && owner !== groupFilter ||
      !['all', 'single', 'ungrouped', rootId].includes(groupFilter) && !state.groups.has(groupFilter));
  }
  function makeJob(id, value) {
    const row = jobTemplate.cloneNode(true);
    const pick = document.createElement("input"); pick.type = "checkbox"; pick.className = "job-pick";
    pick.setAttribute("aria-label", mediaOf(value).title); row.prepend(pick);
    pick.addEventListener("click", event => { event.stopPropagation(); selectJob(id, event, true); });
    const actions = document.createElement("div"); actions.className = "job-actions"; actions.append(row.querySelector(".job-action"));
    const more = document.createElement("button"); more.type = "button"; more.className = "text-button job-more"; more.textContent = t("more"); actions.append(more); row.append(actions);
    row.dataset.job = id; row.tabIndex = 0; row.draggable = true; row.setAttribute("role", "option"); row.querySelector(".job-action").removeAttribute("aria-label");
    if (value.parentId) { row.classList.add('job-child'); row.draggable = false; }
    const titleLine = document.createElement('div'); titleLine.className = 'job-title-line'; row.querySelector('.job-open').before(titleLine); titleLine.append(row.querySelector('.job-open'));
    if (value.collectionId) {
      const toggle = document.createElement('button'); toggle.type = 'button'; toggle.className = 'collection-toggle'; toggle.setAttribute('aria-expanded', String(state.expanded.has(id))); toggle.setAttribute('aria-label', t(state.expanded.has(id) ? 'workbench.collapse_collection' : 'workbench.expand_collection'));
      toggle.append(lucide.createElement(lucide.icons.ChevronDown, {width: 16, height: 16, 'aria-hidden': 'true'}));
      toggle.addEventListener('click', event => { event.stopPropagation(); if (state.expanded.has(id)) state.expanded.delete(id); else state.expanded.add(id); renderTasks(); state.rows.get(id)?.querySelector('.collection-toggle').focus({preventScroll: true}); }); titleLine.prepend(toggle);
    }
    const extra = document.createElement("span"); extra.className = "job-extra"; row.querySelector(".job-content").append(extra);
    const meta = row.querySelector('.job-meta'); meta.replaceChildren();
    const site = document.createElement('span'); site.className = 'job-site';
    for (const kind of ['icon', 'name']) { const element = document.createElement('span'); element.className = 'job-site-' + kind; site.append(element); }
    meta.append(site);
    for (const key of ['format', 'note', 'ext', 'size']) { const field = document.createElement('span'); field.className = 'job-display-' + key; meta.append(field); }
    const status = row.querySelector(".job-state"), speed = document.createElement("span"); speed.className = "job-speed";
    status.replaceChildren(document.createTextNode(""), speed); row.querySelector(".progress-detail")?.remove();
    cover(row.querySelector(".job-cover"), mediaOf(value)); patchJob(row, value);
    row.addEventListener("click", async event => {
      if (event.target.matches("input")) return;
      if (event.target.closest('.job-open')) { clearTimeout(taskTitleClick); state.selectedTask = id; renderInspector(); if (event.detail === 1) taskTitleClick = setTimeout(() => { if (state.view === 'queue') openPanel('summary').catch(report); }, 250); return; }
      selectJob(id, event);
      if (event.target.closest(".job-action")) {
        try { await runItemAction(id); } catch (error) { report(error); }
      }
      if (event.target.closest(".job-more")) openJobMenu(event.target.closest('.job-more'));
      renderInspector();
    });
    row.addEventListener("contextmenu", event => { event.preventDefault(); if (!state.picked.has(id)) selectJob(id, event); state.selectedTask = id; openJobMenu(row.querySelector('.job-more')); });
    row.addEventListener("dragstart", event => { state.dragId = id; event.dataTransfer.setData("application/x-ytdlp-queue-id", id); event.dataTransfer.effectAllowed = "move"; });
    row.addEventListener("dragend", () => { state.dragId = null; });
    row.addEventListener("dragover", event => { if (state.dragId && event.dataTransfer.types.includes("application/x-ytdlp-queue-id")) { event.preventDefault(); event.dataTransfer.dropEffect = "move"; } });
    row.addEventListener("drop", event => {
      if (!state.dragId || event.dataTransfer.getData("application/x-ytdlp-queue-id") !== state.dragId) return;
      event.preventDefault(); const dragged = state.dragId, position = state.positions.get(id); state.dragId = null;
      bridge.request('moveGroupItem', {itemId: dragged, position, groupId: groupOf(id)}).catch(report);
    });
    row.addEventListener("dblclick", () => { clearTimeout(taskTitleClick); state.selectedTask = id; openPanel(state.collections.has(id) ? 'summary' : 'logs').catch(report); });
    state.rows.set(id, row); return row;
  }
  const visibleIds = () => [...state.rows.keys()];
  function selectJob(id, event = {}, toggle = false) {
    const ids = visibleIds();
    if (event.shiftKey && ids.includes(state.anchor)) {
      if (!event.ctrlKey) state.picked.clear();
      const first = ids.indexOf(state.anchor), last = ids.indexOf(id);
      ids.slice(Math.min(first, last), Math.max(first, last) + 1).forEach(key => state.picked.add(key));
    } else {
      if (!event.ctrlKey && !toggle) state.picked.clear();
      if ((event.ctrlKey || toggle) && state.picked.has(id)) state.picked.delete(id); else state.picked.add(id);
      state.anchor = id;
    }
    state.selectedTask = id; state.currentGroup = groupOf(id); renderSelection(event.type === "click" && event.detail === 1 && !toggle); renderInspector();
  }
  function renderSelection(deferBatch = false) {
    for (const [id, row] of state.rows) { row.querySelector('.job-pick').checked = state.picked.has(id); row.classList.toggle('is-selected', state.picked.has(id) || id === state.selectedTask); row.setAttribute('aria-selected', String(state.picked.has(id))); }
    const ids = visibleIds(), count = ids.filter(id => state.picked.has(id)).length;
    q('.select-jobs').checked = !!ids.length && count === ids.length; q('.select-jobs').indeterminate = count > 0 && count < ids.length;
    // 单击出现批量栏时延后位移，双击仍能命中同一个任务。
    if (deferBatch && q('.batch-bar').hidden && state.picked.size && !batchVisibilityTimer) batchVisibilityTimer = setTimeout(() => { batchVisibilityTimer = 0; renderSelection(); }, 500);
    if (!state.picked.size) { clearTimeout(batchVisibilityTimer); batchVisibilityTimer = 0; }
    if (!batchVisibilityTimer) q('.batch-bar').hidden = !state.picked.size;
    q('.batch-count').textContent = t('selected_tasks').replace('{count}', state.picked.size);
    renderSelectionActions();
    renderGroupContext();
  }
  function clearViewSelection() { state.picked.clear(); state.anchor = null; if (!visibleIds().includes(state.selectedTask)) state.selectedTask = null; q('.job-menu').hidden = true; renderTasks(); }
  function reconcileJob(id, value) {
    if (!state.positions.has(id)) { if (value.parentId && state.expanded.has(value.parentId)) renderTasks(); return; }
    let row = state.rows.get(id);
    if (!visibleJob(id, value)) { row?.remove(); state.rows.delete(id); return; }
    if (row) { patchJob(row, value); return; }
    row = makeJob(id, value);
    let next = null;
    const owner = groupOf(id), container = state.groupRows.get(owner)?.list; if (!container) return;
    for (let n = state.positions.get(id) + 1; n < state.items.length && !next; ++n) if (groupOf(state.items[n]) === owner) next = state.rows.get(state.items[n]);
    container.insertBefore(row, next || null);
  }
  function renderTasks() {
    const focused = document.activeElement, focusedJob = focused.matches('.job-more') ? focused.closest('.job')?.dataset.job : null, focusedGroup = focused.matches('.queue-group-more') ? focused.closest('.queue-group')?.dataset.queueGroup : null;
    const scrollTop = q('.jobs').scrollTop;
    if (activeMenu && q('.jobs').contains(activeMenu.anchor)) closeMenu();
    q(".jobs").replaceChildren(); state.rows.clear(); state.groupRows.clear();
    q(".queue-count").textContent = state.items.length;
    q(".queue-total").textContent = state.items.length + " · " + t("tasks");
    q(".queue-caption").textContent = t("core_queue");
    const groupFilter = q(".group-filter").value;
    q(".group-filter").replaceChildren(new Option(t("all_groups"), "all"), new Option(t("ungrouped"), "ungrouped"), new Option(t("individual"), "single"),
      ...[...state.groups.values()].map(group => new Option(group.name, group.groupId)),
      ...[...state.collections.values()].map(value => new Option(value.source.metadata.title, value.collectionId)));
    q(".group-filter").value = [...q(".group-filter").options].some(option => option.value === groupFilter) ? groupFilter : "all";
    const fragment = document.createDocumentFragment();
    for (const group of [...state.groups.values(), state.ungrouped]) {
      const filter = q('.group-filter').value;
      if (state.groups.has(filter) && filter !== group.groupId || filter === 'ungrouped' && group.groupId) continue;
      const section = groupSection(group), list = state.groupRows.get(group.groupId).list;
      for (const id of group.items) {
        const value = model(id); if (!value || !visibleJob(id, value)) continue;
        list.append(makeJob(id, value));
        if (value.children && (state.expanded.has(id) || queueSearch.value.trim() || q('.queue-filter[aria-pressed="true"]').dataset.filter === 'needs')) for (const child of value.children) { const task = model(child); if (task && visibleJob(child, task)) list.append(makeJob(child, task)); }
      }
      fragment.append(section);
    }
    q(".jobs").append(fragment); renderInspector(); renderSelection();
    q('.jobs').scrollTop = scrollTop;
    positionMenu();
    renderGroupContext();
    renderWorkbench();
    if (focusedJob) (state.rows.get(focusedJob)?.querySelector('.job-more') || q('.group-create')).focus({preventScroll: true});
    else if (focusedGroup !== null) (state.groupRows.get(focusedGroup)?.section.querySelector('.queue-group-more') || q('.group-create')).focus({preventScroll: true});
  }
  bridge.on("componentStatusChanged", value => { q(".nav-engine").textContent = t(value.available ? "component_ready" : "component_missing"); });
  const componentName = id => t('component.name.' + id);
  const componentVersion = component => component.currentVersion || t('component.installation.' + component.installation);
  const componentStatus = component => t('component.' + (component.status === 'current' ? component.comparison === 'newer' ? 'ahead' : 'up_to_date' : component.status));
  const componentError = component => [component.error, component.localError].filter(Boolean).map(error => strings[error.messageKey] || error.message || t('component.componentCheckFailed')).join(' · ');
  function renderComponents() {
    if (!state.settings) return;
    const writable = !state.settings.diagnostics.enabled || state.settings.diagnostics.writable;
    q('.component-list').replaceChildren(...[...components.values()].map(component => {
      const row = document.createElement('section'), heading = document.createElement('div'), name = document.createElement('button'), check = document.createElement('button');
      row.className = 'component-item'; row.dataset.component = component.component; heading.className = 'component-heading';
      name.type = check.type = 'button'; name.className = 'text-button component-name cursor-interaction'; name.textContent = componentName(component.component); name.setAttribute('aria-pressed', String(component.component === currentComponent));
      name.addEventListener('click', () => { currentComponent = component.component; renderComponents(); });
      check.className = 'button component-check cursor-interaction'; check.textContent = t('component.check'); check.disabled = component.status === 'checking';
      check.addEventListener('click', async () => { currentComponent = component.component; try { await bridge.request('checkComponents', {components: [component.component]}); } catch (error) { report(error); } renderContext(); });
      heading.append(name, check); row.append(heading);
      for (const [key, value] of [['current', componentVersion(component)], ['latest', component.latestVersion || t('component.not_checked')]]) {
        const line = document.createElement('p'); line.className = 'component-version component-' + key; line.textContent = t('component.' + key) + ' · ' + value; row.append(line);
      }
      const status = document.createElement('p'); status.className = 'component-state'; status.textContent = componentStatus(component); status.classList.toggle('error', ['failed', 'offline'].includes(component.status)); status.setAttribute('role', 'status'); row.append(status);
      const path = document.createElement('p'); path.className = 'component-effective-path small-note'; path.textContent = t('component.effective_path') + ' · ' + (component.path || t('component.notInstalled')); path.title = component.path; row.append(path);
      if (component.channel) { const channel = document.createElement('p'); channel.className = 'small-note component-target-channel'; channel.textContent = t('component.channel') + ' · ' + t('component.' + component.channel); row.append(channel); }
      const error = componentError(component); if (error) { const message = document.createElement('p'); message.className = 'small-note component-error'; message.textContent = error; message.setAttribute('role', 'alert'); row.append(message); }
      if (component.installBlock && component.component !== 'gui') { const limit = document.createElement('p'); limit.className = 'small-note component-install-limit'; limit.textContent = strings['web.update.' + component.installBlock] || strings['web.component.' + component.installBlock] || t('update.installUnavailable'); row.append(limit); }
      const update = componentUpdates.get(component.component), actions = document.createElement('div'); actions.className = 'action-pair';
      if (component.installAvailable && !update?.cancellable && !update?.recoveryRequired) {
        const install = document.createElement('button'); install.type = 'button'; install.className = 'button component-install'; install.textContent = t(component.component === 'ffmpeg' ? 'update.chooseBuild' : component.installation === 'missing' ? 'update.install' : 'update.prepare');
        install.addEventListener('click', async () => { currentComponent = component.component; try { receiveComponentUpdate(await bridge.request('prepareComponentUpdate', {component: component.component})); } catch (error) { report(error); } }); actions.append(install);
      }
      if (component.component !== 'gui') { const notes = document.createElement('button'); notes.type = 'button'; notes.className = 'text-button component-notes'; notes.textContent = t('update.releaseNotes'); notes.addEventListener('click', () => openReleaseNotes(component.component)); actions.append(notes); }
      row.append(actions); if (update) renderComponentUpdate(row, update);
      if (['yt-dlp', 'ffmpeg'].includes(component.component)) { const pathButton = document.createElement('button'); pathButton.type = 'button'; pathButton.className = 'text-button component-path-entry cursor-interaction'; pathButton.textContent = t('component.path_settings'); pathButton.addEventListener('click', () => { currentComponent = component.component; q('.component-path-settings').open = true; q(component.component === 'yt-dlp' ? '.ytdlp-path' : '.ffmpeg-path').focus(); renderContext(); }); row.append(pathButton); }
      return row;
    }));
    for (const [selector, key] of [['.ytdlp-path', 'ytdlpPath'], ['.ffmpeg-path', 'ffmpegDirectory']]) {
      const input = q(selector); if (document.activeElement !== input && !componentSaving) input.value = state.settings.components[key]; input.disabled = !writable || componentSaving; input.placeholder = t('component.default_path');
    }
    if (!componentSaving) q('.update-channel').value = state.settings.components.ytdlpChannel;
    all('.component-pick, .component-save-path, .component-reset-path, .update-channel').forEach(control => { control.disabled = !writable || componentSaving; control.title = ''; });
    q('.component-path-state').textContent = componentFeedback; q('.component-path-state').setAttribute('role', componentFeedbackError ? 'alert' : 'status');
    q('[data-pref="checkStartup"]').checked = state.settings.components.checkAtStartup; q('[data-pref="ffplay"]').checked = state.settings.components.includeFfplay;
    all('[data-pref="checkStartup"], [data-pref="ffplay"]').forEach(control => { control.disabled = !writable || componentSaving; control.title = ''; });
    const available = [...components.values()].filter(component => component.status === 'updateAvailable' && component.installAvailable);
    updateNotice.hidden = !available.length; updateNotice.textContent = t('update.available').replace('{components}', available.map(component => componentName(component.component)).join(' · '));
    const ytdlp = components.get('yt-dlp'); if (ytdlp) q('.nav-engine').textContent = t(ytdlp.installation === 'missing' ? 'component_missing' : 'component_ready');
    renderContext();
  }
  bridge.on('componentChanged', component => { components.set(component.component, component); renderComponents(); if (state.view === 'settings' && !q('[data-category="about"]').hidden) loadAbout().catch(report); });
  function receiveComponentUpdate(value) {
    const before = componentUpdates.get(value.component);
    if (before && (value.generation < before.generation || value.generation === before.generation && (value.updateId !== before.updateId || value.revision <= before.revision))) return;
    componentUpdates.set(value.component, value); renderComponents();
  }
  bridge.on('componentUpdateChanged', receiveComponentUpdate);
  function renderComponentUpdate(row, update) {
    const block = document.createElement('div'); block.className = 'component-update'; block.dataset.updateId = update.updateId; block.dataset.generation = update.generation;
    const status = document.createElement('p'); status.className = 'component-update-state'; status.setAttribute('role', update.error ? 'alert' : 'status'); status.textContent = t('update.state.' + update.state); block.append(status);
    const details = document.createElement('p'); details.className = 'component-update-details'; details.textContent = [update.package.version, update.package.repository, update.package.architecture, ...update.targets].filter(Boolean).join('\n'); block.append(details);
    if (update.state === 'downloading') {
      const progress = document.createElement('progress'); progress.className = 'component-update-progress'; progress.setAttribute('aria-label', t('update.state.downloading'));
      if (update.totalBytes !== null) { progress.max = update.totalBytes; progress.value = update.downloadedBytes; }
      const bytes = document.createElement('p'); bytes.className = 'component-update-bytes small-note'; bytes.textContent = sizeText(update.downloadedBytes) + ' / ' + (update.totalBytes === null ? t('update.unknownTotal') : sizeText(update.totalBytes)); block.append(progress, bytes);
    } else if (update.totalFiles) { const files = document.createElement('p'); files.className = 'small-note'; files.textContent = t('update.files').replace('{done}', update.completedFiles).replace('{total}', update.totalFiles); block.append(files); }
    if (update.state === 'waitingConfirmation') {
      const note = document.createElement('p'); note.className = 'small-note'; note.textContent = t('update.integrity'); block.append(note);
      if (update.customPath) { const label = document.createElement('label'), input = document.createElement('input'), text = document.createElement('span'); label.className = 'check-row'; input.type = 'checkbox'; input.className = 'component-custom-confirm'; input.checked = customUpdateConfirmations.get(update.updateId) || false; text.textContent = t('update.customConfirm'); label.append(input, text); input.addEventListener('change', () => { customUpdateConfirmations.set(update.updateId, input.checked); renderComponents(); }); block.append(label); }
    }
    if (update.error) { const error = document.createElement('p'); error.className = 'component-update-error small-note'; error.textContent = [update.failedStage && t('update.state.' + update.failedStage), update.error.code, String(update.error.systemError), strings[update.error.messageKey] || t('update.updateFailed')].filter(Boolean).join(' · '); block.append(error); }
    if (update.recoveryError) { const error = document.createElement('p'); error.className = 'component-update-error small-note'; error.textContent = update.recoveryError.code + ' · ' + update.recoveryError.systemError + ' · ' + (strings[update.recoveryError.messageKey] || t('update.rollbackFailed')); block.append(error); }
    if (update.backupDirectory) { const backup = document.createElement('p'); backup.className = 'component-update-details'; backup.textContent = t('update.backup') + '\n' + update.backupDirectory + '\n.ytdlp-update-' + update.updateId + '-*.bak'; block.append(backup); }
    if (update.state === 'installed') { const result = document.createElement('p'); result.className = 'component-update-result'; result.textContent = t('update.verifiedVersion') + ' · ' + update.resultVersion; block.append(result); }
    if (!update.cancellable && ['replacing', 'rollingBack'].includes(update.state)) { const note = document.createElement('p'); note.className = 'small-note'; note.textContent = t('update.commitCannotCancel'); block.append(note); }
    const actions = document.createElement('div'); actions.className = 'action-pair';
    for (const [command, key, shown] of [['confirmComponentUpdate', 'confirm', update.state === 'waitingConfirmation'], ['cancelComponentUpdate', 'cancel', update.cancellable], ['recoverComponentUpdate', 'recover', update.recoveryRequired && !['rollingBack', 'replacing'].includes(update.state)]]) {
      if (!shown) continue; const button = document.createElement('button'); button.type = 'button'; button.className = 'button component-' + key; button.textContent = t('update.' + key); button.disabled = command === 'confirmComponentUpdate' && update.customPath && !customUpdateConfirmations.get(update.updateId);
      button.addEventListener('click', async () => { button.disabled = true; try { await bridge.request(command, {updateId: update.updateId, generation: update.generation, confirmed: true, customPathConfirmed: !!customUpdateConfirmations.get(update.updateId)}); } catch (error) { report(error); renderComponents(); } }); actions.append(button);
    }
    block.append(actions); row.append(block);
  }
  async function saveComponent(command, payload) {
    componentSaving = true; ++settingsPending; componentFeedback = t('component.saving'); componentFeedbackError = false; renderComponents();
    const operation = settingsChain.then(async () => {
      try { state.settings = await bridge.request(command, payload); components = new Map((await bridge.request('getComponents')).map(component => [component.component, component])); componentFeedback = t('component.saved'); }
      catch (error) { componentFeedback = error.message || t('component.componentCheckFailed'); componentFeedbackError = true; }
      finally { componentSaving = false; --settingsPending; renderSettings(); if (componentFeedbackError) { q('.settings-save-state').textContent = componentFeedback; q('.settings-save-state').setAttribute('role', 'alert'); } renderComponents(); }
    });
    settingsChain = operation.catch(report); return operation;
  }
  const componentInput = id => q(id === 'yt-dlp' ? '.ytdlp-path' : '.ffmpeg-path');
  all('.component-save-path').forEach(button => button.addEventListener('click', () => { currentComponent = button.dataset.componentPath; saveComponent('setComponentPath', {component: currentComponent, path: componentInput(currentComponent).value}); }));
  all('.component-reset-path').forEach(button => button.addEventListener('click', () => { currentComponent = button.dataset.componentPath; saveComponent('setComponentPath', {component: currentComponent, path: ''}); }));
  all('.component-pick').forEach(button => button.addEventListener('click', async () => {
    currentComponent = button.dataset.componentPath;
    try { const result = await bridge.request('chooseComponentPath', {component: currentComponent}); if (!result.cancelled) await saveComponent('setComponentPath', {component: currentComponent, path: result.file || result.folder}); }
    catch (error) { componentFeedback = error.message; componentFeedbackError = true; renderComponents(); }
  }));
  q('.update-channel').addEventListener('change', () => { currentComponent = 'yt-dlp'; saveComponent('setComponentChannel', {channel: q('.update-channel').value}); });
  all('[data-pref="checkStartup"], [data-pref="ffplay"]').forEach(input => input.addEventListener('change', () => saveComponent('setComponentUpdatePreferences', {checkAtStartup: q('[data-pref="checkStartup"]').checked, includeFfplay: q('[data-pref="ffplay"]').checked})));
  async function openReleaseNotes(component, page = 1) {
    const request = ++releaseRequest, previous = releaseNotes?.component === component ? releaseNotes.releases : [];
    panel = {kind: 'changes', from: 'settings'}; all('.detail-panel').forEach(element => element.hidden = element.dataset.detail !== 'changes'); q('.panel-title').textContent = t('update.releaseNotes'); q('.panel-context').textContent = componentName(component); go('detail');
    releaseNotes = {component, page, releases: previous, loading: true}; renderReleaseNotes();
    try { const value = await bridge.request('loadReleaseNotes', {component, page}); if (request !== releaseRequest) return; releaseNotes = {...value, loading: false}; }
    catch (error) { if (request !== releaseRequest) return; releaseNotes = {component, page, releases: [], loading: false, error: error.message}; }
    renderReleaseNotes();
  }
  function renderReleaseNotes() {
    if (!releaseNotes) return; const value = releaseNotes, select = q('.release-version'), chosen = select.value;
    q('.release-component').replaceChildren(...['yt-dlp', 'ffmpeg', 'deno'].map(component => new Option(componentName(component), component))); q('.release-component').value = value.component;
    select.replaceChildren(...value.releases.map((release, index) => new Option(release.tag + ' · ' + (release.publishedAt || t('update.unknownDate')), String(index)))); select.value = [...select.options].some(option => option.value === chosen) ? chosen : '0'; select.disabled = value.loading || q('.changelog-view').checked;
    q('.release-status').textContent = value.error || t(value.loading ? 'update.loadingNotes' : value.releases.length ? 'update.notesAsText' : 'update.noReleases');
    const selected = value.releases[Number(select.value)]; q('.changes-content').textContent = (q('.changelog-view').checked ? value.releases : selected ? [selected] : []).map(release => release.tag + ' · ' + (release.publishedAt || t('update.unknownDate')) + '\n\n' + (release.body || t('update.noNotes'))).join('\n\n────────────────────\n\n');
    q('.release-prev').disabled = value.loading || value.page <= 1; q('.release-next').disabled = value.loading || !value.hasMore; q('.release-open').disabled = value.loading || !selected; renderContext();
  }
  q('.release-component').addEventListener('change', event => openReleaseNotes(event.target.value)); q('.release-version').addEventListener('change', renderReleaseNotes); q('.changelog-view').addEventListener('change', renderReleaseNotes);
  q('.release-prev').addEventListener('click', () => openReleaseNotes(releaseNotes.component, releaseNotes.page - 1)); q('.release-next').addEventListener('click', () => openReleaseNotes(releaseNotes.component, releaseNotes.page + 1));
  q('.release-open').addEventListener('click', () => { const release = releaseNotes.releases[Number(q('.release-version').value)]; if (release) bridge.request('openComponentRelease', {component: releaseNotes.component, channel: state.settings.components.ytdlpChannel, tag: release.tag}).catch(report); });
  q('.changes-content').addEventListener('wheel', event => { if (!releaseNotes?.releases.length || q('.changelog-view').checked || detailScroll.scrollHeight > detailScroll.clientHeight + 1) return; const select = q('.release-version'), count = select.options.length; select.value = String((Number(select.value) + (event.deltaY > 0 ? 1 : count - 1)) % count); renderReleaseNotes(); event.preventDefault(); });
  bridge.on("mediaParsing", (_, id) => { if (id === state.parseId) q(".source-state").textContent = t("parsing"); });
  bridge.on("mediaParsed", (media, id) => {
    if (id !== state.parseId) return;
    state.parsing = false; state.media = media; state.selected = true;
    q(".source-state").textContent = t("parse_success"); renderSource(); say(t("parse_success"));
  });
  function showSource(source) {
    if (state.sources.has(source.sourceId)) state.sources.set(source.sourceId, source);
    state.source = source; state.media = source.kind === "media" ? source.entries[0]?.media : source.metadata;
    state.selected = !!source.selectedEntries.length; renderSource();
  }
  function loadChannelPlaylists(source) {
    if (source.kind !== "channel" || state.channels.has(source.sourceId)) return;
    const channel = {root: source, playlists: source.playlists, loading: source.category !== "playlists" && !source.playlists.length, error: null};
    state.channels.set(source.sourceId, channel);
    if (!channel.loading) return;
    const request = bridge.request("parseSource", {url: source.url, category: "playlists"});
    state.sourceRequests.set(request.id, {sourceId: source.sourceId, catalog: true});
    request.catch(error => sourceRequestEvent(null, error, request.id));
  }
  function sourceRequestEvent(source, error, id) {
    const pending = state.sourceRequests.get(id); if (!pending) return false;
    state.sourceRequests.delete(id);
    if (pending.result) {
      const result = pending.result; result.status = error ? 'failed' : 'ready'; result.error = error;
      state.batchEvents.set(result.requestId, {source, error});
      if (source) { state.sources.set(source.sourceId, source); loadChannelPlaylists(source); if (!state.source) showSource(source); }
      state.parsing = state.inputResults.some(result => ['accepted', 'parsing'].includes(result.status)) || [...state.sourceRequests.values()].some(value => !value.catalog);
      q('.source-state').textContent = error?.message || t('parse_success'); renderSource(); if (error) report(error); else say(t('parse_success')); return true;
    }
    const current = state.sources.get(pending.sourceId), channel = state.channels.get(pending.sourceId);
    if (!current) return true;
    if (pending.catalog) { channel.loading = false; channel.error = error; if (source) channel.playlists = source.playlists; }
    else {
      state.parsing = state.inputResults.some(result => ["accepted", "parsing"].includes(result.status)) || [...state.sourceRequests.values()].some(value => !value.catalog);
      const result = state.inputResults.find(result => state.batchEvents.get(result.requestId)?.source?.sourceId === current.sourceId);
      if (result) { result.error = error; if (error) result.retry = {url: pending.url, category: pending.category}; else delete result.retry; }
      if (source) {
        state.sources.delete(current.sourceId); state.sources.set(source.sourceId, source);
        for (const event of state.batchEvents.values()) if (event.source?.sourceId === current.sourceId) event.source = source;
        for (const request of state.sourceRequests.values()) if (request.sourceId === current.sourceId) request.sourceId = source.sourceId;
        if (channel) { state.channels.delete(current.sourceId); if (source.kind !== 'channel') state.channels.set(source.sourceId, channel); }
        if (source.kind === 'channel') loadChannelPlaylists(source);
        if (state.source?.sourceId === current.sourceId) showSource(source);
        q('.source-state').textContent = t('parse_success'); say(t('parse_success'));
      } else { q('.source-state').textContent = error.message; report(error); }
    }
    renderSource(); return true;
  }
  async function replaceSource(source, url, category = "") {
    state.parsing = true; renderSource(); q('.source-state').textContent = t('parsing'); say(t('parsing'));
    const request = bridge.request("parseSource", {url, category, sourceId: source.sourceId, singleVideo: source.kind === 'media' && q('[data-flag="singleVideo"]').checked});
    state.sourceRequests.set(request.id, {sourceId: source.sourceId, catalog: false, url, category});
    try { await request; } catch (error) { sourceRequestEvent(null, error, request.id); }
  }
  async function retrySource(result) {
    if (result.status === 'ready') { const source = state.sources.get(state.batchEvents.get(result.requestId)?.source?.sourceId); if (source) return replaceSource(source, result.retry?.url || source.url, result.retry?.category || source.category); }
    result.status = 'parsing'; result.error = null; state.parsing = true; renderSource();
    const request = bridge.request('parseSources', {text: q('.link-box textarea').value, inputIndex: result.inputIndex, singleVideo: q('[data-flag="singleVideo"]').checked});
    const childId = request.id + ':' + result.inputIndex; state.sourceRequests.set(childId, {result, catalog: false});
    try { const reply = await request; const item = reply.results[0]; if (item.status !== 'accepted') sourceRequestEvent(null, {message: item.error?.message || t('input.' + item.status)}, childId); }
    catch (error) { sourceRequestEvent(null, error, childId); }
  }
  function batchEvent(source, error, id) {
    if (!state.batchId || !id.startsWith(state.batchId + ":")) return false;
    state.batchEvents.set(id, {source, error});
    const result = state.inputResults.find(result => result.requestId === id);
    if (result) { result.status = error ? "failed" : "ready"; if (error) result.error = error; }
    if (source) { state.sources.set(source.sourceId, source); loadChannelPlaylists(source); if (!state.source) showSource(source); }
    state.parsing = state.inputResults.some(result => result.status === "accepted" || result.status === "parsing");
    renderSource(); q('.source-state').textContent = t(state.parsing ? 'parsing' : state.sources.size ? 'parse_success' : 'parse_failed');
    if (!state.parsing) { if (error && !state.source) report(error); else say(q('.source-state').textContent, !state.sources.size); }
    return true;
  }
  bridge.on("sourceParsed", (source, id) => {
    if (sourceRequestEvent(source, null, id)) return;
    if (batchEvent(source, null, id)) return;
    if (id !== state.parseId) return;
    for (const [key, value] of state.sources) if (value.url === source.url) state.sources.delete(key);
    state.sources.set(source.sourceId, source);
    state.parsing = false; showSource(source);
    q(".source-state").textContent = t("parse_success"); say(t("parse_success"));
  });
  bridge.on("sourceSelectionChanged", source => {
    if (state.sources.has(source.sourceId) && source.revision >= state.sources.get(source.sourceId).revision) state.sources.set(source.sourceId, source);
    if (source.sourceId === state.source?.sourceId && source.revision >= state.source.revision) showSource(source);
    else if (selectionSources().length > 1) renderSource();
  });
  bridge.on("sourceParseFailed", (error, id) => {
    if (sourceRequestEvent(null, error, id)) return;
    if (batchEvent(null, error, id)) return;
    if (id !== state.parseId) return;
    state.parsing = false; state.source = null; state.media = null; renderSource(); report(error);
  });
  bridge.on("mediaParseFailed", (error, id) => {
    if (id !== state.parseId) return;
    state.parsing = false; state.media = null; state.selected = false;
    q(".source-state").textContent = error.message; renderSource(); report(error);
  });
  function updateTask(task) {
    if ((state.tasks.get(task.taskId)?.revision || 0) > task.revision) return;
    const previousStatus = state.tasks.get(task.taskId)?.status, previousRefreshing = state.tasks.get(task.taskId)?.refreshing;
    state.tasks.set(task.taskId, task);
    if (previousStatus !== task.status) reconcileJob(task.taskId, task);
    else { const row = state.rows.get(task.taskId); if (row) patchJob(row, task); }
    if (sidebarTaskId() === task.taskId) renderInspector();
    if (panel?.kind === 'summary' && panel.mediaId === task.taskId) renderTaskSummary();
    if (previousStatus !== task.status || previousRefreshing !== task.refreshing) { renderSelectionActions(); renderGroupContext(); }
    renderWorkbench();
  }
  function updateCollection(value) {
    if ((state.collections.get(value.collectionId)?.summary.revision || 0) > value.summary.revision) return;
    const previousStatus = state.collections.get(value.collectionId)?.summary.status;
    state.collections.set(value.collectionId, value);
    if (previousStatus !== value.summary.status) reconcileJob(value.collectionId, value);
    else { const row = state.rows.get(value.collectionId); if (row) patchJob(row, value); }
    if (sidebarTaskId() === value.collectionId) renderInspector();
    if (panel?.kind === 'summary' && panel.mediaId === value.collectionId) renderTaskSummary();
  }
  function updateCollectionSummary(value) {
    const collection = state.collections.get(value.collectionId);
    if (!collection || collection.summary.revision > value.revision) return;
    const previousStatus = collection.summary.status;
    collection.summary = value;
    if (previousStatus !== value.status) reconcileJob(value.collectionId, collection);
    else { const row = state.rows.get(value.collectionId); if (row) patchJob(row, collection); }
    if (sidebarTaskId() === value.collectionId) renderInspector();
    if (previousStatus !== value.status) { renderSelectionActions(); renderGroupContext(); }
  }
  function showQueue(queue) {
    if (queue.revision < state.queueRevision) return;
    state.queueRevision = queue.revision;
    state.tasks = new Map(queue.tasks.map(task => [task.taskId, task]));
    state.collections = new Map((queue.collections || []).map(value => [value.collectionId, value]));
    state.items = queue.items || queue.tasks.map(task => task.taskId);
    state.groups = new Map((queue.groups || []).map(group => [group.groupId, group])); state.ungrouped = queue.ungrouped || {groupId: '', items: state.items, summary: {}};
    state.itemGroups = new Map([...state.groups.values()].flatMap(group => group.items.map(id => [id, group.groupId])));
    if (state.currentGroup && !state.groups.has(state.currentGroup)) state.currentGroup = '';
    state.positions = new Map(state.items.map((id, index) => [id, index]));
    for (const id of state.picked) if (!model(id)) state.picked.delete(id);
    if (!model(state.selectedTask)) state.selectedTask = null;
    renderTasks();
  }
  bridge.on("taskAdded", task => { state.selectedTask = task.parentId || task.taskId; updateTask(task); });
  for (const name of ["taskOptionsChanged", "taskUpdated", "taskProgress", "taskStatusChanged"]) bridge.on(name, updateTask);
  bridge.on("taskRemoved", task => {
    state.tasks.delete(task.taskId); state.picked.delete(task.taskId);
    state.logs.delete(task.taskId); if (lastLogView?.taskId === task.taskId) lastLogView = null;
    if (state.selectedTask === task.taskId) state.selectedTask = null;
    state.rows.get(task.taskId)?.remove(); state.rows.delete(task.taskId);
  });
  bridge.on("queueChanged", showQueue);
  bridge.on("queueStateChanged", queue => {
    if (queue.revision < state.queueRevision) return;
    state.queueRevision = queue.revision;
    renderGroupContext();
  });
  bridge.on("collectionAdded", value => { state.selectedTask = value.collectionId; updateCollection(value); });
  bridge.on("collectionUpdated", updateCollection);
  for (const event of ["collectionProgress", "collectionStatusChanged"]) bridge.on(event, updateCollectionSummary);
  bridge.on('queueGroupProgress', value => { const group = value.groupId ? state.groups.get(value.groupId) : state.ungrouped; if (!group || (group.summary.revision || 0) > value.summary.revision) return; group.summary = value.summary; patchGroupHeader(group); renderGroupContext(); });
  bridge.on("collectionRemoved", value => {
    state.collections.delete(value.collectionId); state.picked.delete(value.collectionId);
    state.rows.get(value.collectionId)?.remove(); state.rows.delete(value.collectionId);
  });
  bridge.on("persistenceFailed", status => { persistenceError = status.error; report(status.error); });
  bridge.on("persistenceRecovered", () => { persistenceError = null; say(t("ready")); });
  bridge.on("taskCompleted", task => { updateTask(task); say(t("download_success")); });
  bridge.on("taskFailed", task => { updateTask(task); report(task.error); });
  async function parseSource(category = "") {
    if (category) { const source = state.source; return replaceSource(source, state.channels.get(source.sourceId)?.root.url || source.url, category); }
    const text = q(".link-box textarea").value.trim();
    const processing = options().processing;
    loadOptions({...options(), processing: {...processing, formatIds: [], multiAudio: false, sections: [], filename: "",
      subLangs: state.settings.defaults.processing.subLangs, subFormat: state.settings.defaults.processing.subFormat,
      edited: processing.edited.filter(key => key !== "subTracks")}});
    state.parsing = true; state.media = null; state.source = null; state.selected = false; state.editing = null; state.collectionEditing = null; renderSource();
    q('.source-state').textContent = t('parsing'); say(t('parsing'));
    const request = bridge.request("parseSources", {text, singleVideo: q('[data-flag="singleVideo"]').checked});
    state.parseId = null; state.batchId = request.id;
    state.sources.clear(); state.batchEvents.clear(); state.channels.clear(); state.sourceRequests.clear(); state.inputResults = [];
    try {
      const result = await request;
      if (state.batchId === request.id) {
        state.inputResults = result.results;
        for (const [id, event] of state.batchEvents) batchEvent(event.source, event.error, id);
        state.parsing = state.inputResults.some(result => result.status === "accepted"); renderSource();
        if (!state.parsing && !state.source) { q('.source-state').textContent = state.inputResults.map(result => t('input.' + result.status)).join(' · ') || t('invalid_url'); say(q('.source-state').textContent, true); }
      }
    } catch (error) { state.parsing = false; q(".source-state").textContent = error.message; renderSource(); report(error); }
  }
  q(".analyze-link").addEventListener("click", () => parseSource());
  q(".channel-tab").addEventListener("change", event => parseSource(event.target.value));
  sourceFocus.addEventListener('change', () => { const source = state.sources.get(sourceFocus.value); if (source) showSource(source); });
  sourceAvailability.addEventListener('change', renderSource);
  reparseSources.addEventListener('click', () => { for (const source of selectionSources()) replaceSource(source, source.url, source.category); });
  cancelSource.addEventListener('click', async () => {
    cancelSource.disabled = true;
    const ids = [...new Set([state.batchId, ...[...state.sourceRequests].filter(([, value]) => !value.catalog).map(([id]) => id)].filter(Boolean))];
    const replies = await Promise.allSettled(ids.map(requestId => bridge.request('cancelSourceParse', {requestId})));
    const error = replies.find(value => value.status === 'rejected' && value.reason.code !== 'sourceParseNotActive'); if (error) report(error.reason);
  });
  function clearInput() { state.media = null; state.source = null; state.selected = false; state.parseId = null; state.batchId = null; state.parsing = false; state.collectionEditing = null; state.sources.clear(); state.channels.clear(); state.sourceRequests.clear(); state.inputResults = []; renderSource(); }
  q(".link-box textarea").addEventListener("input", clearInput);
  function sourceDrop(event) {
    const editing = event.target.closest('textarea,input:not([type="checkbox"]):not([type="radio"]),[contenteditable="true"]');
    return state.view === "source" && (!editing || editing === q(".link-box textarea")) && event.dataTransfer.types.some(type => ['text/uri-list', 'text/plain', 'Files'].includes(type));
  }
  product.addEventListener("dragover", event => { if (sourceDrop(event)) { event.preventDefault(); event.dataTransfer.dropEffect = 'copy'; } });
  product.addEventListener("drop", event => {
    if (!sourceDrop(event)) return;
    event.preventDefault(); if (event.dataTransfer.types.includes('Files')) { event.dataTransfer.dropEffect = 'none'; say(t('invalid_url'), true); return; }
    const text = event.dataTransfer.getData("text/plain") || event.dataTransfer.getData("text/uri-list").split(/\r?\n/).filter(line => line && !line.startsWith("#")).join('\n');
    q(".link-box textarea").value = text; clearInput(); parseSource();
  });
  q(".select-all").addEventListener("click", () => { for (const source of selectionSources()) changeSelection((_ids, entries) => entries.filter(entry => entry.available).map(entry => entry.entryId), source); });
  q(".select-none").addEventListener("click", () => { for (const source of selectionSources()) changeSelection(() => [], source); });
  q(".toggle-range").addEventListener("click", () => { q(".range-bar").hidden = !q(".range-bar").hidden; });
  q(".apply-range").addEventListener("click", () => {
    const text = q(".range-input").value.trim();
    if (!/^[1-9]\d*(?:-[1-9]\d*)?(?:,[1-9]\d*(?:-[1-9]\d*)?)*$/.test(text)) { say(t("invalid_range"), true); return; }
    const ranges = text.split(",").map(value => value.split("-").map(Number));
    const sources = selectionSources(), count = sources.reduce((count, source) => count + source.entries.length, 0);
    if (ranges.some(([first, last = first]) => last < first || last > count)) { say(t("invalid_range"), true); return; }
    let offset = 0;
    for (const source of sources) {
      const start = offset;
      changeSelection((_ids, entries) => entries.filter((entry, index) => {
        const position = sources.length > 1 ? start + index + 1 : entry.index;
        return entry.available && ranges.some(([first, last = first]) => position >= first && position <= last);
      }).map(entry => entry.entryId), source);
      offset += source.entries.length;
    }
  });
  q(".next-output").addEventListener("click", async () => {
    if (state.collectionEditing) {
      if (!await confirmScope(selectionNote.textContent)) return;
      try { await bridge.request('selectCollection', {collectionId: state.collectionEditing, entryIds: state.source.selectedEntries}); state.collectionEditing = null; go('queue'); say(t('selection_saved')); } catch (error) { report(error); }
      return;
    }
    try { const sourceId = state.source?.sourceId; state.editing = null; outputBackup = options(); const prepared = await bridge.request('prepareTaskOptions', {options: options()}); if (state.view !== 'source' || state.source?.sourceId !== sourceId) return; loadOptions(prepared); go('output'); }
    catch (error) { report(error); }
  });
  q(".edit-job").addEventListener("click", () => {
    const task = model(sidebarTaskId()); if (!task) return;
    panel = null;
    outputBackup = options(); state.editing = task.collectionId || task.taskId; loadOptions(task.options); go("output");
  });
  q(".reselect-job").addEventListener("click", () => {
    const value = state.collections.get(state.selectedTask); if (!value) return;
    state.collectionEditing = value.collectionId; showSource(structuredClone(value.source)); go("source");
  });
  const folderButton = q(".folder-input").parentElement.querySelector("button");
  folderButton.addEventListener("click", async () => {
    try { const result = await bridge.request("chooseFolder", {folder: q(".folder-input").value}); if (!result.cancelled) { q(".folder-input").value = result.folder; markEdited("folder"); renderOutput(); } } catch (error) { report(error); }
  });
  q(".preset-select").addEventListener("change", event => {
    const format = event.target.value;
    if (format === "custom") return;
    if (format.startsWith("preset:")) { loadOptions(state.settings.presets[Number(format.slice(7))].options); return; }
    const processing = {...options().processing, formatIds: [], multiAudio: false, edited: [...new Set([...processingDraft.edited, "format", "sort", "cover", "metadata"])]};
    loadOptions({...options(), kind: format === "mp4" ? "video" : "audio", format, quality: "1080", cover: format !== "mp4", metadata: format !== "mp4", processing});
  });
  all('input[name="download-kind"]').forEach(input => input.addEventListener("change", () => {
    const before = q('.format-select').value; markEdited('format'); processingDraft.formatIds = []; setKind(input.value);
    if ([...q('.format-select').options].some(option => option.value === before)) q('.format-select').value = before;
    const next = q('.format-select').value; formatNotice.hidden = before === next; formatNotice.textContent = before === next ? '' : t('polish.format_fallback').replace('{before}', before.toUpperCase()).replace('{after}', next.toUpperCase()); renderOutput();
  }));
  for (const [selector, key] of Object.entries({".format-select": "format", ".audio-treatment": "format", ".quality-select": "sort", ".cover-check": "cover", ".metadata-check": "metadata", ".folder-input": "folder", ".naming-select": "naming"})) q(selector).addEventListener("change", () => { markEdited(key); if (key === "format") processingDraft.formatIds = []; renderOutput(); });
  all('.advanced-options input, .advanced-options select, .advanced-options textarea').forEach(control => control.addEventListener("change", () => {
    const key = control.dataset.flag || control.dataset.extra || (control.dataset.choice ? "sort" : "");
    if (key) markEdited(["higherFps", "premium"].includes(key) ? "sort" : key);
    renderOutput();
  }));
  q('.output-form').addEventListener('input', event => { if (!event.target.matches('textarea,input[type="text"],input[type="number"]')) return; clearTimeout(outputInputTimer); outputInputTimer = setTimeout(renderOutput, 150); });
  q('.output-form').addEventListener('focusin', event => { parameterSection = event.target.closest('[data-output-section]')?.dataset.outputSection || ''; renderContext(); });
  all('[data-output-section]').forEach(element => element.addEventListener('toggle', () => { if (element.open) parameterSection = element.dataset.outputSection; else if (!all('[data-output-section]').some(section => section.open)) parameterSection = ''; renderContext(); }));
  async function enqueue(start) {
    if (state.busy) return;
    if (state.editing && state.editing !== 'defaults' && state.settings.taskOptionsScope.mode === 'shared' && !await confirmScope(sharedScopeMessage())) return;
    state.busy = true; renderOutput();
    try {
      await bridge.request('previewOutput', {options: options()});
      if (state.editing === "defaults") {
        await saveSettings({...state.settings, defaults: options()}); state.editing = null; outputBackup = null; go("settings"); say(t("defaults_saved"));
      } else if (state.editing) {
        const group = state.collections.has(state.editing);
        await bridge.request(group ? "updateCollectionDefaults" : "updateTaskOptions", group ? {collectionId: state.editing, options: options()} : {taskId: state.editing, options: options()}); state.editing = null;
        if (outputBackup) loadOptions(outputBackup); outputBackup = null;
        go("queue"); say(t(state.settings.taskOptionsScope.mode === 'shared' ? 'shared_edit_saved' : group ? "collection_defaults_only" : "task_saved"));
      } else {
        const added = [];
        for (const source of selectionSources().filter(source => source.selectedEntries.length)) added.push(await bridge.request("addCollection", {sourceId: source.sourceId, mode: q(".grouping-select").value, options: options()}));
        go("queue"); say(t("task_added"));
        if (start) await bridge.request('operateQueueScope', {action: 'start', scope: 'selection', itemIds: added.flatMap(value => value.collectionId ? [value.collectionId] : value.taskIds)});
        const folder = options().outputFolder;
        if (!state.settings.folders.includes(folder)) await saveSettings({...state.settings, folders: [folder, ...state.settings.folders]});
      }
    } catch (error) { report(error); }
    finally { state.busy = false; renderOutput(); }
  }
  q(".enqueue-button").addEventListener("click", () => enqueue(false));
  q(".download-button").addEventListener("click", () => enqueue(true));
  all("[data-go]").forEach(button => button.addEventListener("click", () => {
    if (!["home", "source", "queue", "output", "naming", "settings"].includes(button.dataset.go)) { say(t("not_migrated")); return; }
    if (button.dataset.go === "output" && state.view === "naming") { cancelNaming(); return; }
    if (button.dataset.go === "naming") { openNaming(); return; }
    if (button.dataset.go === 'source' && !drawer.open) { state.editing = null; state.collectionEditing = null; outputBackup = options(); clearInput(); q('.link-box textarea').value = ''; loadOptions(state.settings.newTaskOptions); }
    go(button.dataset.go === "source" && state.editing ? "queue" : button.dataset.go);
  }));
  all(".queue-filter").forEach(button => button.addEventListener("click", () => {
    all(".queue-filter").forEach(other => other.setAttribute("aria-pressed", String(other === button))); clearViewSelection();
  }));
  q('.group-filter').addEventListener('change', () => { const value = q('.group-filter').value; state.currentGroup = value === 'ungrouped' ? '' : state.groups.has(value) ? value : null; clearViewSelection(); });
  q('.queue-scope').addEventListener('change', renderGroupContext);
  q(".select-jobs").addEventListener("change", event => {
    for (const id of visibleIds()) if (event.target.checked) state.picked.add(id); else state.picked.delete(id);
    renderTasks();
  });
  q(".clear-selection").addEventListener("click", () => { state.picked.clear(); q(".select-jobs").checked = false; renderTasks(); });
  all("[data-batch]").forEach(button => button.addEventListener("click", async () => {
    const action = button.dataset.batch;
    if (!["start", "stop", "remove", "clearDone", "copy", "skip", "unskip", "toggleSkip", "refresh"].includes(action)) return;
    if (availabilityDialog.contains(button)) closeAvailability();
    const allTasks = button.dataset.all === "true";
    const scope = allTasks ? q('.queue-scope').value : 'selection', taskIds = scopeItems(scope);
    if (action === "copy") { try { await copyClipboard({kind: 'urls', ids: taskIds}); } catch (error) { report(error); } return; }
    if (['start', 'stop', 'remove', 'clearDone'].includes(action)) { try { await applyScope(action === 'clearDone' ? 'clearCompleted' : action, scope); } catch (error) { report(error); } return; }
    const command = action === "toggleSkip" ? "invertDownloadable" : action === "refresh" ? "refreshMetadata" : action === "clearDone" ? "clearCompleted" : action === "remove" ? "removeTasks" :
      action === "start" ? (allTasks ? "startAll" : "startTasks") : action === "skip" ? "skipTasks" : action === "unskip" ? "unskipTasks" : (allTasks ? "stopAll" : "stopTasks");
    try { await bridge.request(command, {taskIds}); } catch (error) { report(error); }
  }));
  q(".close-job-menu").addEventListener("click", closeMenu);
  all(".job-menu [data-task]").forEach(button => button.addEventListener("click", async () => {
    const taskId = state.selectedTask, action = button.dataset.task;
    if (!taskId) return;
    try {
      if (action === "up" || action === "down") {
        const owner = groupOf(taskId), siblings = owner ? state.groups.get(owner).items : state.ungrouped.items, target = siblings[siblings.indexOf(taskId) + (action === 'up' ? -1 : 1)];
        await bridge.request('moveGroupItem', {itemId: taskId, position: state.positions.get(target), groupId: owner});
      } else if (action === "file") await bridge.request("openFile", {taskId});
      else if (action === "folder") await bridge.request("revealTaskFile", {taskId});
      else if (action === "refresh") await bridge.request("refreshMetadata", {taskIds: [taskId]});
      else if (action === "split") await bridge.request("splitCollection", {collectionId: taskId});
      else if (action === "skip") await bridge.request(summary(model(taskId)).status === 'skipped' ? "unskipTasks" : "skipTasks", {taskIds: [taskId]});
      else if (action === "copy") await copyClipboard({kind: 'urls', ids: [taskId]});
    } catch (error) { report(error); }
  }));
  q('[data-flag="singleVideo"]').addEventListener("change", () => { if (state.source && !state.editing) { parseSource(); go("source"); } });
  async function previewNaming(draft) {
    const revision = ++namingRevision; q('.use-naming').disabled = true; q('.save-naming').disabled = true;
    namingStatus = t('context.checking'); renderContext();
    if (draft.parts) namingRule = {...draft};
    try {
      const result = await bridge.request('previewNaming', {...draft, extension: namingDraft.format});
      if (revision !== namingRevision || !namingDraft) return;
      let validation = t(result.canApply ? 'context.valid' : 'context.empty_naming');
      if (result.canApply) try { await bridge.request('previewOutput', {options: {...options(), naming: 'custom', outputTemplate: result.outputTemplate}}); } catch (error) { validation = error.message; }
      if (revision !== namingRevision || !namingDraft) return;
      namingRule = result; q('.raw-template').value = result.outputTemplate; q('.separator-select').value = result.separator;
      all('[data-name-preset]').forEach(button => button.setAttribute('aria-pressed', String(result.outputTemplate === namingTemplate(button.dataset.namePreset))));
      all('.saved-naming button').forEach((button, index) => button.setAttribute('aria-pressed', String(result.outputTemplate === state.settings.namingPresets[index].outputTemplate)));
      q('.name-tokens').replaceChildren(...result.labels.map((label, index) => { const token = document.createElement('span'), order = document.createElement('small'); token.className = 'name-token'; order.textContent = String(index + 1); token.append(order, document.createTextNode(label)); return token; }));
      q('.name-preview-value').textContent = result.filename;
      pathValue(q('.name-preview-value'), result.filename, true); namingStatus = validation; renderContext();
      q('.use-naming').disabled = !result.canApply;
      q('.save-naming').disabled = !result.canApply || state.settings.diagnostics.enabled && !state.settings.diagnostics.writable;
      all('[data-name-field]').forEach(button => button.disabled = false);
      q('.remove-name-field').disabled = !result.parts.length; q('.clear-name-fields').disabled = !result.parts.length;
    } catch (error) { if (revision === namingRevision) { namingRule = null; q('.remove-name-field').disabled = true; q('.name-preview-value').textContent = error.message; namingStatus = error.message; renderContext(); report(error); } }
  }
  function openNaming() {
    namingDraft = structuredClone(options()); namingRule = null;
    q('.raw-template').value = namingDraft.outputTemplate || namingTemplate(namingDraft.naming);
    all('[data-name-field]').forEach(button => button.disabled = true); go('naming');
    previewNaming({outputTemplate: q('.raw-template').value});
  }
  function cancelNaming() { ++namingRevision; if (namingDraft) loadOptions(namingDraft); namingDraft = null; namingRule = null; go("output"); }
  q(".use-naming").addEventListener("click", async () => {
    if (!namingDraft || !namingRule?.canApply) return;
    const value = options(), original = namingDraft.outputTemplate || namingTemplate(namingDraft.naming);
    const changed = q('.raw-template').value !== original || value.bandcampTemplate !== namingDraft.bandcampTemplate || value.playlistPrefix !== namingDraft.playlistPrefix || value.zeroPadding !== namingDraft.zeroPadding || value.playlistFolder !== namingDraft.playlistFolder;
    if (q('.raw-template').value !== original) { value.naming = 'custom'; value.outputTemplate = q('.raw-template').value; }
    try {
      await bridge.request('previewOutput', {options: value});
      namingDraft = null; namingRule = null; ++namingRevision; loadOptions(value); if (changed) markEdited('naming'); renderOutput(); go('output');
    } catch (error) { report(error); }
  });
  q(".cancel-naming").addEventListener("click", cancelNaming);
  q(".reset-template").addEventListener("click", () => previewNaming({outputTemplate: namingTemplate("title")}));
  q(".reset-prefix").addEventListener("click", () => { q(".playlist-prefix").value = state.settings.naming.prefixDefault; });
  q('.raw-template').addEventListener('input', () => previewNaming({outputTemplate: q('.raw-template').value}));
  for (const selector of ['.bandcamp-template', '.playlist-prefix', '[data-extra="zeroPadding"]', '[data-extra="playlistFolder"]']) q(selector).addEventListener('change', () => { if (state.view === 'naming') previewNaming({outputTemplate: q('.raw-template').value}); });
  all('[data-name-field]').forEach(button => button.addEventListener('click', () => previewNaming({parts: [...(namingRule?.parts || []), {id: button.dataset.nameField}], separator: q('.separator-select').value})));
  q('.separator-select').addEventListener('change', () => previewNaming({parts: namingRule?.parts || [], separator: q('.separator-select').value}));
  q('.remove-name-field').addEventListener('click', () => previewNaming({parts: namingRule.parts.slice(0, -1), separator: q('.separator-select').value}));
  q('.clear-name-fields').addEventListener('click', () => previewNaming({parts: [], separator: q('.separator-select').value}));
  q('.save-naming').addEventListener('click', async () => {
    try { const result = await presetCommand('saveNamingPreset', {outputTemplate: q('.raw-template').value}); say(t('naming_saved').replace('{name}', result.name)); }
    catch (error) { report(error); }
  });
  q(".cancel-arguments").addEventListener("click", cancelDrawer);
  function commitSettings(update, renderQueue = renderTasks) {
    ++settingsPending; q(".settings-save-state").textContent = t("settings_saving");
    q('.settings-save-state').setAttribute('role', 'status');
    const operation = settingsChain.then(async () => {
      try {
        state.settings = await bridge.request("saveSettings", {settings: update(await bridge.request("loadSettings"))});
        if (settingsPending === 1) { renderSettings(); renderQueue(); say(t(state.settings.diagnostics.restartRequired ? "settings_restart_required" : "settings_autosaved")); }
        return state.settings;
      } catch (error) { q(".settings-save-state").textContent = error.message; q(".settings-save-state").setAttribute("role", "alert"); throw error; }
      finally { --settingsPending; renderConfigControls(); }
    });
    settingsChain = operation.catch(() => {}); return operation;
  }
  function saveSettings(value) {
    const changes = Object.fromEntries(["defaults", "presets", "arguments", "folders"].filter(key => JSON.stringify(value[key]) !== JSON.stringify(state.settings[key])).map(key => [key, value[key]]));
    return commitSettings(current => ({...current, ...changes}));
  }
  function renderCompletion() {
    if (!state.settings) return;
    const action = state.settings.finishAction || 'none', caps = state.desktop?.completion || {};
    const writable = !state.settings.diagnostics.enabled || state.settings.diagnostics.writable;
    all('[data-finish-text]').forEach(element => element.textContent = t(element.dataset.finishText));
    all('.finish-action input').forEach(input => {
      if (!finishSaving) input.checked = input.value === action; input.disabled = finishSaving || !writable || !caps[input.value];
      input.title = caps[input.value] ? '' : t('completion_unavailable');
    });
    q('.finish-current').textContent = finishSaving ? t('settings_saving') : t('completion_selected').replace('{action}', t('completion_action.' + action));
    const current = completion || {}, pending = current.state === 'pending';
    q('.finish-countdown').hidden = !['pending', 'executing', 'failed'].includes(current.state);
    const message = current.state === 'failed' ? current.error?.message + ' (' + current.error?.systemError + ')' :
      t(pending ? 'completion_countdown' : 'completion_executing').replace('{action}', t('completion_action.' + current.action)).replace('{seconds}', current.seconds);
    q('.countdown-title').textContent = message; q('.countdown-title').title = message;
    q('.finish-countdown').setAttribute('role', current.state === 'failed' ? 'alert' : 'status');
    q('.cancel-finish').hidden = !pending; q('.cancel-finish').disabled = finishCancelling; q('.cancel-finish').textContent = t('completion_cancel');
  }
  function receiveCompletion(value) {
    if (!value) return;
    if (completion && completion.revision > value.revision) return;
    const previous = completion; completion = value; renderCompletion();
    if (value.state === 'cancelled' && (previous?.state !== value.state || previous?.token !== value.token))
      say(t(value.reason === 'background' ? 'completion_background' : 'completion_cancelled'));
    if (value.state === 'executed') say(t(state.desktop?.completion.simulated ? 'completion_simulated' : 'completion_submitted'));
    if (value.state === 'failed') say(value.error.message + ' (' + value.error.systemError + ')', true);
  }
  bridge.on('completionChanged', receiveCompletion);
  q('[data-settings="queue"]').addEventListener('click', async () => {
    try { state.desktop = await bridge.request('desktopStatus'); renderCompletion(); }
    catch (error) { report(error); }
  });
  all('.finish-action input').forEach(input => input.addEventListener('change', () => {
    const action = input.value; finishSaving = true; renderCompletion();
    const operation = settingsChain.then(async () => {
      try { const value = await bridge.request('setFinishAction', {action}); state.settings = await bridge.request('loadSettings'); receiveCompletion(value); say(t('settings_autosaved')); }
      catch (error) { report(error); }
      finally { finishSaving = false; renderCompletion(); }
    });
    settingsChain = operation.catch(() => {});
  }));
  q('.cancel-finish').addEventListener('click', async () => {
    finishCancelling = true; renderCompletion();
    try { receiveCompletion(await bridge.request('cancelCompletion', {token: completion.token})); }
    catch (error) { report(error); }
    finally { finishCancelling = false; renderCompletion(); }
  });
  for (const [key, selector] of Object.entries(displayFields)) q(selector).addEventListener('change', event => {
    const value = event.target.checked; say(t('settings_saving'));
    commitSettings(current => ({...current, application: {...current.application, appearance: {...current.application.appearance,
      queueDisplay: {...current.application.appearance.queueDisplay, [key]: value}}}}), renderQueueDisplay).catch(error => { renderQueueDisplay(); report(error); });
  });
  function presetCommand(command, payload) {
    ++settingsPending; q('.settings-save-state').textContent = t('settings_saving');
    q('.settings-save-state').setAttribute('role', 'status');
    const operation = settingsChain.then(async () => {
      try { const result = await bridge.request(command, payload); state.settings = result.settings; renderSettings(); renderTasks(); return result; }
      catch (error) { q('.settings-save-state').textContent = error.message; q('.settings-save-state').setAttribute('role', 'alert'); throw error; }
      finally { --settingsPending; renderConfigControls(); }
    });
    settingsChain = operation.catch(() => {}); renderConfigControls(); return operation;
  }
  function renderConfigControls() {
    const selected = q('.config-presets').value !== '', writable = !state.settings.diagnostics.enabled || state.settings.diagnostics.writable;
    all('.config-editor button, .config-editor input, .config-editor select').forEach(control => control.title = '');
    all('[data-config]').forEach(button => { button.disabled = settingsPending > 0 || !writable || button.dataset.config !== 'create' && !selected; button.title = ''; });
    q('.config-presets').disabled = settingsPending > 0; q('.config-name').disabled = settingsPending > 0 || !writable;
    q('.confirm-blank-config').disabled = settingsPending > 0 || !writable; q('.cancel-blank-config').disabled = settingsPending > 0;
  }
  function renderOptionScope() {
    q('.queue-option-scope').value = state.settings.taskOptionsScope.mode;
    q('.queue-option-scope').disabled = scopePending || !state.queueWritable;
    q('.queue-option-scope').title = state.queueWritable ? '' : t('queue_save_failed');
    q('.queue-option-scope').closest('label').querySelector('small').textContent = t('processing_scope_description');
    q('.default-output-scope').textContent = t(state.settings.taskOptionsScope.mode === 'shared' ? 'default_scope_shared' : 'default_scope_individual');
    renderInspector();
  }
  async function scopeOperation(command, payload) {
    scopePending = true; renderOptionScope();
    try {
      const result = await bridge.request(command, payload);
      state.settings = await bridge.request('loadSettings'); showQueue(await bridge.request('listQueue')); renderSettings();
      say(t(command === 'copyTaskProcessingOptions' ? 'processing_copied' : 'processing_scope_saved').replace('{count}', result.updatedTaskIds.length).replace('{deferred}', result.deferredTaskIds.length).replace('{completed}', result.completedTaskIds.length));
    } catch (error) { report(error); }
    finally { scopePending = false; renderOptionScope(); }
  }
  q('.queue-option-scope').addEventListener('change', async event => { const mode = event.target.value; renderOptionScope(); if (await confirmScope(mode === 'shared' ? sharedScopeMessage() : t('workbench.individual_confirmation'))) await scopeOperation('setTaskOptionsScope', {mode, sourceId: state.selectedTask || ''}); });
  q('.copy-options').addEventListener('click', async () => {
    const sourceId = state.selectedTask;
    try {
      const current = await bridge.request('listQueue'), source = current.tasks.find(task => task.taskId === sourceId) || current.collections.find(value => value.collectionId === sourceId);
      if (!source) { say(t('select_task_first'), true); return; }
      const text = t('polish.copy_source').replace('{title}', mediaOf(source).title) + '\n' + t('workbench.shared_confirmation').replace('{count}', current.tasks.length) + '\n' + current.tasks.map(task => task.media.title + ' · ' + statusText(task.status)).join('\n');
      if (await confirmScope(text)) await scopeOperation('copyTaskProcessingOptions', {sourceId});
    } catch (error) { report(error); }
  });
  bridge.on('taskOptionsScopeChanged', async () => {
    if (scopePending) return;
    try { const current = await bridge.request('loadSettings'); state.settings.taskOptionsScope = current.taskOptionsScope; renderOptionScope(); }
    catch (error) { report(error); }
  });
  async function configAction(action, allowBlank = false) {
    const selected = state.settings.applicationPresets[Number(q('.config-presets').value)], name = q('.config-name').value;
    if (['create', 'rename'].includes(action) && !name && !allowBlank) { blankConfigAction = action; q('.blank-config-confirm').hidden = false; return; }
    q('.blank-config-confirm').hidden = true; blankConfigAction = null;
    const command = {create: 'saveApplicationPreset', save: 'saveApplicationPreset', load: 'applyApplicationPreset', rename: 'renameApplicationPreset', delete: 'deleteApplicationPreset'}[action];
    const payload = action === 'create' ? {name} : action === 'rename' ? {name: selected.name, newName: name} : {name: selected.name, replace: action === 'save'};
    try {
      await presetCommand(command, payload);
      if (action === 'create' || action === 'rename') q('.config-presets').value = String(state.settings.applicationPresets.findIndex(preset => preset.name === name));
      if (action === 'delete') { q('.config-presets').value = ''; q('.config-name').value = ''; }
      renderConfigControls(); q('.config-status').textContent = t('config_' + action + '_done'); q('.config-status').setAttribute('role', 'status'); say(q('.config-status').textContent);
    } catch (error) { q('.config-status').textContent = error.message; q('.config-status').setAttribute('role', 'alert'); report(error); }
  }
  q('.config-presets').addEventListener('change', event => { q('.config-name').value = event.target.value === '' ? '' : state.settings.applicationPresets[Number(event.target.value)].name; renderConfigControls(); });
  all('[data-config]').forEach(button => button.addEventListener('click', () => configAction(button.dataset.config)));
  q('.confirm-blank-config').addEventListener('click', () => configAction(blankConfigAction, true));
  q('.cancel-blank-config').addEventListener('click', () => { blankConfigAction = null; q('.blank-config-confirm').hidden = true; q('.config-status').textContent = t('config_cancelled'); });
  const settingFields = {
    '.proxy-enabled': ["network", "proxyEnabled"], '.proxy-input': ["network", "proxy"], '.cookie-mode': ["authentication", "mode"],
    '.cookie-browser-select': ["authentication", "browser"], '.cookie-browser input': ["authentication", "browserOptions"], '.cookie-file input': ["authentication", "cookieFile"],
    '.aria-enabled': ["externalDownloader", "enabled"], '.aria-args': ["externalDownloader", "arguments"],
    '.sponsor-mark-enabled': ["contentProcessing", "sponsorMark"], '.sponsor-remove-enabled': ["contentProcessing", "sponsorRemove"],
    '[data-pref="maxDownloads"]': ["scheduler", "maxConcurrentDownloads"], '[data-pref="maxInfo"]': ["scheduler", "maxConcurrentMetadata"],
    '[data-pref="lengthy"]': ["scheduler", "startNextDuringProcessing"], '[data-pref="nextOnStop"]': ["scheduler", "continueAfterStop"],
    '[data-pref="autoStart"]': ["queueBehavior", "autoStartRecoveredQueue"], '[data-pref="saveErrors"]': ["queueBehavior", "restoreFailed"], '[data-pref="removeDone"]': ["queueBehavior", "autoRemoveCompleted"],
    '[data-pref="clipboardFocus"]': ["applicationBehavior", "clipboardOnFocus"], '[data-pref="customTitle"]': ["applicationBehavior", "displayCustomFilenames"],
    '[data-pref="folderOrigin"]': ["applicationBehavior", "folderOrigin"], '.language-select': ["applicationBehavior", "language"],
    '.theme-mode': ["appearance", "theme"], '.contrast-slider': ["appearance", "contrast"],
    '[data-pref="snap"]': ["appearance", "snap"], '[data-pref="noMinWidth"]': ["appearance", "noMinWidth"], '[data-pref="exactBytes"]': ["appearance", "exactBytes"]
  };
  function saveApplicationField(group, key, value) {
    if (group === "network") {
      const network = {proxyEnabled: q('.proxy-enabled').checked, proxy: q('.proxy-input').value};
      q('.proxy-input').disabled = !network.proxyEnabled;
      if (network.proxyEnabled && !network.proxy) { q('.settings-save-state').textContent = t("proxy_address_required"); return Promise.resolve(); }
      return commitSettings(current => ({...current, application: {...current.application, network}}));
    }
    if (group === "authentication") {
      const authentication = {mode: q('.cookie-mode').value, browser: q('.cookie-browser-select').value, browserOptions: q('.cookie-browser input').value, cookieFile: key === "cookieFile" ? value : q('.cookie-file input').value};
      q('.cookie-browser').hidden = authentication.mode !== "browser"; q('.cookie-file').hidden = authentication.mode !== "file";
      if (authentication.mode === "file" && !authentication.cookieFile) { q('.settings-save-state').textContent = t("cookie_file_required"); return Promise.resolve(); }
      return commitSettings(current => ({...current, application: {...current.application, authentication}}));
    }
    return commitSettings(current => ({...current, application: {...current.application, [group]: {...current.application[group], [key]: value}}}));
  }
  for (const [selector, [group, key]] of Object.entries(settingFields)) q(selector).addEventListener("change", event => {
    const input = event.target, value = input.type === "checkbox" ? input.checked : ["number", "range"].includes(input.type) ? Number(input.value) : input.value;
    saveApplicationField(group, key, value).catch(report);
  });
  q('.contrast-slider').addEventListener('input', event => { state.settings.application.appearance.contrast = Number(event.target.value); applyAppearance(); });
  q('.cookie-file .native-pick').addEventListener("click", async () => {
    try { const result = await bridge.request("chooseCookieFile"); if (!result.cancelled) await saveApplicationField("authentication", "cookieFile", result.file); } catch (error) { report(error); }
  });
  function renderSettings() {
    const currentPreset = q(".preset-select").value;
    q(".preset-select").replaceChildren(...["mp4", "m4a", "mp3"].map(key => new Option(t("preset." + key), key)), new Option(t("custom_settings"), "custom"),
      ...state.settings.presets.map((preset, index) => new Option(preset.name, "preset:" + index)));
    q(".preset-select").value = [...q(".preset-select").options].some(option => option.value === currentPreset) ? currentPreset : "custom";
    q(".folder-history").replaceChildren(new Option(t("choose_history"), ""), ...state.settings.folders.map(folder => new Option(folder, folder)));
    q(".raw-history").replaceChildren(new Option(t("choose_history"), ""), ...state.settings.arguments.map((entry, index) => new Option(entry.label || entry.arguments, String(index))));
    const config = q('.config-presets').selectedOptions[0]?.dataset.name;
    q('.config-presets').replaceChildren(new Option(t('choose_config'), ''), ...state.settings.applicationPresets.map((preset, index) => { const option = new Option(preset.name || t('blank_config_name'), String(index)); option.dataset.name = preset.name; return option; }));
    const configIndex = state.settings.applicationPresets.findIndex(preset => preset.name === config); q('.config-presets').value = configIndex < 0 ? '' : String(configIndex);
    q('.saved-naming').replaceChildren(...state.settings.namingPresets.map(preset => {
      const button = document.createElement('button'); button.type = 'button'; button.className = 'scheme-button cursor-interaction'; button.textContent = preset.name; button.setAttribute('aria-pressed', String(preset.outputTemplate === q('.raw-template').value));
      button.addEventListener('click', () => previewNaming({outputTemplate: preset.outputTemplate})); return button;
    }));
    q('.saved-heading').textContent = t(state.settings.namingPresets.length ? 'saved_naming' : 'no_saved_naming');
    q(".default-output-summary").textContent = state.settings.defaults.format.toUpperCase() + " · " + t("naming." + state.settings.defaults.naming);
    const app = state.settings.application, diagnostics = state.settings.diagnostics, writable = !diagnostics.enabled || diagnostics.writable;
    for (const [selector, [group, key]] of Object.entries(settingFields)) {
      const input = q(selector); if (input.type === "checkbox") input.checked = app[group][key]; else if (document.activeElement !== input) input.value = app[group][key];
      input.disabled = !writable; input.title = "";
    }
    q('.edit-defaults').disabled = !writable; q('.cookie-file .native-pick').disabled = !writable; q('.cookie-file .native-pick').title = "";
    q('.clear-folders').disabled = !writable || !state.settings.folders.length; q('.save-arguments').disabled = !writable; renderConfigControls();
    q('.proxy-input').disabled = !writable || !app.network.proxyEnabled;
    q('.cookie-browser').hidden = app.authentication.mode !== "browser"; q('.cookie-file').hidden = app.authentication.mode !== "file";
    q('.aria-enabled').disabled = !writable || !state.settings.capabilities.ariaAvailable && !app.externalDownloader.enabled;
    q('.aria-args').disabled = !writable || !app.externalDownloader.enabled;
    q('.aria-status').textContent = t(state.settings.capabilities.ariaAvailable ? "aria_available" : "aria_missing");
    q('[data-pref="maxInfo"]').max = state.settings.capabilities.maxMetadataConcurrency;
    for (const kind of ["mark", "remove"]) all('.sponsor-' + kind + ' input').forEach(input => { input.checked = app.contentProcessing[kind + "Categories"].includes(input.value); input.disabled = !writable; });
    q(".settings-save-state").textContent = t(!writable ? "settings_readonly" : diagnostics.restartRequired ? "settings_restart_required" : "settings_autosaved");
    q(".settings-save-state").setAttribute("role", writable ? "status" : "alert");
    q(".subtitle-default-state").textContent = state.settings.defaults.processing.subLangs || t("no_subtitle_default");
    renderOptionScope();
    renderQueueDisplay();
    renderCompletion();
    renderComponents();
    applyAppearance();
    renderWorkbench();
    if (panel?.kind === 'formats') all('.format-size').forEach(element => element.textContent = sizeText(Number(element.dataset.bytes)));
  }
  bridge.on('desktopChanged', value => { state.desktop = value; applyAppearance(); renderCompletion(); });
  bridge.on('desktopFailed', report);
  bridge.on("clipboardUrl", value => { if (state.parsing || state.busy || state.editing) return; q('.link-box textarea').value = value.url; go("source"); parseSource(); });
  bridge.on("settingsChanged", async () => { if (settingsPending) return; try { const settings = await bridge.request("loadSettings"); if (settingsPending || settings.revision <= state.settings.revision) return; state.settings = settings; renderSettings(); renderTasks(); } catch (error) { report(error); } });
  q(".edit-defaults").addEventListener("click", () => { outputBackup = options(); state.editing = "defaults"; loadOptions(state.settings.defaults); go("output"); });
  async function loadAbout() {
    about = await bridge.request('loadAbout');
    const facts = q('.about-facts'); facts.replaceChildren();
    for (const [key, value] of [['programVersion', about.programVersion], ['buildRevision', about.buildRevision], ['architecture', about.architecture]]) { const dt = document.createElement('dt'), dd = document.createElement('dd'); dt.textContent = t('about.' + key); dd.textContent = value || t('about.unknown'); facts.append(dt, dd); }
    const dependency = (container, name, version, kind, id, note = '') => {
      const section = document.createElement('section'), title = document.createElement('strong'), info = document.createElement('p'), link = document.createElement('button');
      title.textContent = name; info.className = 'small-note'; info.textContent = t('about.' + kind) + (version ? ' · ' + version : '') + (note ? ' · ' + note : '');
      link.type = 'button'; link.className = 'text-button'; link.textContent = t('about.source'); link.dataset.aboutLink = id;
      link.addEventListener('click', () => bridge.request('openAboutLink', {linkId: id}).catch(report)); section.append(title, info, link); container.append(section);
    };
    q('.about-dependencies').replaceChildren();
    for (const item of about.dependencies) dependency(q('.about-dependencies'), item.name, item.version, item.versionKind, item.id, strings[item.noteKey] || '');
    q('.about-components').replaceChildren();
    for (const component of about.components.filter(item => item.component !== 'gui')) dependency(q('.about-components'), component.component, component.currentVersion, component.currentVersion ? 'installed' : 'unknown', component.component, component.path);
    q('.about-license').textContent = about.license; q('.about-lucide-license').textContent = about.lucideLicense;
    q('[data-about-link="bundledLicenses"]').hidden = !about.bundledLicensesAvailable;
    q('.about-license-state').textContent = t(about.bundledLicensesAvailable ? 'about.licensesAvailable' : 'about.licensesMissing'); renderContext();
  }
  all('[data-about-link]').forEach(button => button.addEventListener('click', () => bridge.request('openAboutLink', {linkId: button.dataset.aboutLink}).catch(report)));
  all("[data-settings]").forEach(button => button.addEventListener("click", () => {
    if (button.disabled) return;
    settingsCategory = button.dataset.settings;
    all(".settings-category").forEach(category => category.hidden = category.dataset.category !== button.dataset.settings);
    go('settings'); q('.settings-layout').scrollTop = 0;
    if (button.dataset.settings === 'about') loadAbout().catch(report);
    renderContext();
  }));
  all('[data-settings-jump="updates"]').forEach(button => button.addEventListener('click', () => { currentComponent = 'ffmpeg'; q('[data-settings="updates"]').click(); }));
  q(".folder-history").addEventListener("change", event => { if (event.target.value) { q(".folder-input").value = event.target.value; markEdited("folder"); renderOutput(); } });
  q(".clear-folders").addEventListener("click", () => saveSettings({...state.settings, folders: []}).catch(report));
  q(".raw-history").addEventListener("change", event => {
    if (event.target.value === "") return;
    const entry = state.settings.arguments[Number(event.target.value)]; q(".raw-input").value = entry.arguments; q(".raw-label").value = entry.label; q(".raw-enabled").checked = true; renderOutput();
  });
  q(".raw-save").addEventListener("click", async () => {
    const argumentsText = q(".raw-input").value, label = q(".raw-label").value; if (!argumentsText.trim()) return;
    const entries = state.settings.arguments.map(entry => ({...entry})), index = entries.findIndex(entry => entry.arguments === argumentsText);
    if (index < 0) entries.push({arguments: argumentsText, label}); else entries[index].label = label;
    try { await saveSettings({...state.settings, arguments: entries}); say(t("settings_saved")); } catch (error) { report(error); }
  });
  q(".raw-delete").addEventListener("click", () => {
    const index = q(".raw-history").value; if (index !== "") saveSettings({...state.settings, arguments: state.settings.arguments.filter((_, n) => n !== Number(index))}).catch(report);
  });
  q(".save-arguments").addEventListener("click", async () => {
    try { const result = await presetCommand('saveDownloadPreset', {options: options()}); q('.preset-select').value = 'preset:' + state.settings.presets.findIndex(preset => preset.name === result.name); say(t('download_preset_saved').replace('{name}', result.name)); } catch (error) { report(error); }
  });
  q(".reset-arguments").addEventListener("click", () => { loadOptions({...options(), processing: structuredClone(processingDefaults)}); });
  const panelMediaId = () => state.view === 'detail' ? panel?.mediaId : state.view === "output" ? state.tasks.has(state.editing) ? state.editing : state.source?.kind === "media" ? state.source.entries[0].url : null : state.selectedTask;
  async function refreshedDetails(id) {
    let details = await bridge.request("loadMediaDetails", {mediaId: id});
    if (!details.metadataReady && details.taskId) {
      await bridge.request("refreshMetadata", {taskIds: [id]});
      for (let n = 0; n < 180; ++n) {
        await new Promise(resolve => setTimeout(resolve, 250)); details = await bridge.request("loadMediaDetails", {mediaId: id});
        if (!details.refreshing) break;
      }
      if (!details.metadataReady) throw new Error(t("refresh_first"));
    }
    return details;
  }
  async function openPanel(kind) {
    const mediaId = kind === 'summary' ? sidebarTaskId() : panelMediaId(); if (!mediaId || kind !== 'summary' && state.collections.has(mediaId)) return;
    if (kind === 'summary') {
      if (state.view === 'detail' && panel.kind === 'summary') return;
      const previous = state.view === 'detail' ? panel : null;
      panel = {kind, mediaId, from: state.view, previous}; q('.panel-title').textContent = t('panel.summary'); q('.panel-context').textContent = mediaOf(model(mediaId)).title;
      all('.detail-panel').forEach(element => element.hidden = element.dataset.detail !== kind); go('detail'); renderTaskSummary(); return;
    }
    const from = state.view === 'detail' ? panel.from : state.view, details = ['logs', 'filename'].includes(kind) ? null : await refreshedDetails(mediaId);
    panel = {kind, mediaId, from, details, options: structuredClone(from === "output" ? options() : state.tasks.get(mediaId).options)};
    if (kind === "sections" && !details.sectionsAvailable) { panel = null; throw new Error(t("sections_unavailable")); }
    q(".panel-title").textContent = t("panel." + kind); q(".panel-context").textContent = details?.metadata.title || state.tasks.get(mediaId)?.media.title || "";
    all(".detail-panel").forEach(element => element.hidden = element.dataset.detail !== kind);
    q(".job-menu").hidden = true; go("detail");
    if (kind === "formats") renderFormats();
    if (kind === "subtitles") renderSubtitles();
    if (kind === "sections") { q(".section-start").value = ""; q(".section-end").value = ""; q(".section-error").textContent = ""; renderSections(); }
    if (kind === "filename") q(".custom-filename").value = panel.options.processing.filename;
    if (kind === "json") { q(".metadata-command").value = details.metadataCommand; renderMetadata(); }
    if (kind === "logs") { await loadLog(mediaId); renderLog(); }
    renderInspector(); renderContext();
  }
  all("[data-panel]").forEach(button => button.addEventListener("click", () => { if (!button.disabled) openPanel(button.dataset.panel).catch(report); }));
  all(".close-panel").forEach(button => button.addEventListener("click", () => {
    if (panel?.kind === 'logs') rememberLogView();
    if (panel?.previous) { panel = panel.previous; q('.panel-title').textContent = t('panel.' + panel.kind); q('.panel-context').textContent = panel.details?.metadata.title || mediaOf(model(panel.mediaId))?.title || ''; all('.detail-panel').forEach(element => element.hidden = element.dataset.detail !== panel.kind); go('detail'); renderLog(); return; }
    const from = panel?.from || 'queue'; panel = null; go(from);
  }));
  function renderTaskSummary() {
    const value = model(panel?.mediaId); if (!value) return;
    const task = summary(value), media = mediaOf(value), scrollTop = detailScroll.scrollTop; summaryPanel.replaceChildren();
    for (const [label, text] of [['task_title', media.title], ['task_status', statusText(task.status)], ['task_source', value.source?.url || value.url], ['task_output', value.options.format.toUpperCase()], ['task_naming', t('naming.' + value.options.naming)], ['task_path', value.options.outputFolder], ['task_filename', task.file || value.options.processing.filename || ''], ['task_error', task.error?.message || '']]) {
      if (!text) continue; const field = document.createElement('label'), name = document.createElement('span'), content = document.createElement('textarea');
      field.className = 'field'; name.textContent = t(label); content.readOnly = true; content.rows = 2; content.value = text; field.append(name, content); summaryPanel.append(field);
    }
    const copy = document.createElement('button'); copy.type = 'button'; copy.className = 'button'; copy.textContent = t('copy_url');
    copy.addEventListener('click', () => copyClipboard({kind: 'urls', ids: [panel.mediaId]}).catch(report)); summaryPanel.append(copy);
    detailScroll.scrollTop = scrollTop;
  }
  async function applyPanel() {
    const proposed = panel.options;
    await bridge.request("previewOutput", {options: proposed, mediaId: panel.mediaId});
    if (panel.from === "output") loadOptions(proposed);
    else await bridge.request("updateTaskOptions", {taskId: panel.mediaId, options: proposed});
    const from = panel.from; panel = null; go(from); say(t("task_saved"));
  }
  function renderFormats() {
    const list = q(".format-streams"); list.replaceChildren(); q(".multi-audio").checked = panel.options.processing.multiAudio;
    cover(q(".format-cover"), panel.details.metadata);
    q(".media-facts > span:last-child").textContent = duration(panel.details.metadata.duration) + " · " + (panel.details.metadata.uploader || "");
    for (const kind of ["mixed", "video", "audio"]) {
      const group = document.createElement("section"); group.className = "stream-group";
      const heading = document.createElement("h2"); heading.textContent = t("stream." + kind); group.append(heading);
      for (const format of panel.details.formats.filter(format => format.kind === kind)) {
        const row = document.createElement("div"); row.className = "stream-row";
        const top = document.createElement("div"); top.className = "stream-heading";
        const label = document.createElement("label"), input = document.createElement("input"); input.type = "checkbox"; input.value = format.format_id; input.checked = panel.options.processing.formatIds.includes(input.value);
        label.append(input, document.createTextNode(format.format_id + " · " + (format.format_note || format.resolution || format.ext || ""))); top.append(label); row.append(top);
        const bytes = format.filesize ?? format.filesize_approx;
        if (bytes != null) { const size = document.createElement('span'); size.className = 'format-size'; size.dataset.bytes = bytes; size.textContent = sizeText(bytes); top.append(size); }
        const technical = document.createElement("details"), summary = document.createElement("summary"), pre = document.createElement("pre");
        summary.textContent = t("technical_fields"); pre.textContent = JSON.stringify(format, null, 2); technical.append(summary, pre); row.append(technical); group.append(row);
        input.addEventListener("change", () => { q(".apply-formats").disabled = !all(".format-streams input:checked").length; });
      }
      list.append(group);
    }
    q(".apply-formats").disabled = !panel.options.processing.formatIds.length;
  }
  q(".apply-formats").addEventListener("click", () => {
    panel.options.processing.formatIds = all(".format-streams input:checked").map(input => input.value); panel.options.processing.multiAudio = q(".multi-audio").checked;
    panel.options.processing.edited = [...new Set([...panel.options.processing.edited, "format"])]; applyPanel().catch(report);
  });
  q(".automatic-format").addEventListener("click", () => { panel.options.processing.formatIds = []; panel.options.processing.multiAudio = false; panel.options.processing.edited = [...new Set([...panel.options.processing.edited, "format"])]; applyPanel().catch(report); });
  function renderSubtitles() {
    const tracks = q(".subtitle-tracks"); tracks.replaceChildren(); const selected = panel.options.processing.subLangs.split(",");
    const formats = new Set();
    for (const track of panel.details.subtitles) {
      const label = document.createElement("label"); label.className = "check-row"; const input = document.createElement("input"); input.type = "checkbox"; input.value = track.language; input.checked = selected.includes(track.language);
      label.append(input, document.createTextNode(track.name + " · " + track.language)); tracks.append(label); track.formats.forEach(format => formats.add(format));
    }
    q(".subtitle-empty").hidden = !!panel.details.subtitles.length;
    q(".subtitle-format").replaceChildren(new Option(t("automatic"), "any"), ...[...formats].map(format => new Option(format, format)));
    q(".subtitle-format").value = panel.options.processing.subFormat || "any";
    q(".subtitle-default").checked = false;
  }
  q(".apply-subtitles").addEventListener("click", async () => {
    panel.options.processing.subLangs = all(".subtitle-tracks input:checked").map(input => input.value).join(",");
    panel.options.processing.subFormat = q(".subtitle-format").value === "any" ? "" : q(".subtitle-format").value;
    panel.options.processing.edited = [...new Set([...panel.options.processing.edited, "subTracks"])];
    try {
      await bridge.request("previewOutput", {options: panel.options, mediaId: panel.mediaId});
      if (q(".subtitle-default").checked) await saveSettings({...state.settings, defaults: {...state.settings.defaults, processing: {...state.settings.defaults.processing, subLangs: panel.options.processing.subLangs, subFormat: panel.options.processing.subFormat}}});
      await applyPanel();
    } catch (error) { report(error); }
  });
  q(".clear-subtitle-default").addEventListener("click", () => saveSettings({...state.settings, defaults: {...state.settings.defaults, processing: {...state.settings.defaults.processing, subLangs: "", subFormat: ""}}}).catch(report));
  function renderSections() {
    q(".section-list").replaceChildren();
    panel.options.processing.sections.forEach((section, index) => {
      const row = document.createElement("div"); row.className = "section-row"; const label = document.createElement("span"); label.textContent = (section.start || "0") + " → " + (section.end && section.end !== "0" ? section.end : t("to_end"));
      const remove = document.createElement("button"); remove.type = "button"; remove.className = "text-button"; remove.textContent = t("remove"); remove.addEventListener("click", () => { panel.options.processing.sections.splice(index, 1); renderSections(); }); row.append(label, remove); q(".section-list").append(row);
    });
  }
  q(".add-section").addEventListener("click", async () => {
    const sections = [...panel.options.processing.sections, {start: q(".section-start").value, end: q(".section-end").value}];
    try {
      await bridge.request("previewOutput", {options: {...panel.options, processing: {...panel.options.processing, sections}}, mediaId: panel.mediaId});
      panel.options.processing.sections = sections; q(".section-error").textContent = ""; renderSections();
    } catch (error) { q(".section-error").textContent = error.message; }
  });
  q(".clear-sections").addEventListener("click", () => { panel.options.processing.sections = []; renderSections(); });
  q(".apply-sections").addEventListener("click", () => applyPanel().catch(report));
  q('.detail-panel[data-detail="filename"] .native-pick').addEventListener("click", async () => { try { const value = await bridge.request("chooseOutputFile", {folder: panel.options.outputFolder}); if (!value.cancelled) q(".custom-filename").value = value.filename; } catch (error) { report(error); } });
  async function applyFilename(clear) {
    const filename = clear ? "" : q(".custom-filename").value;
    if (panel.from === "output") {
      const normalized = await bridge.request("normalizeFilename", {options: panel.options, filename}); loadOptions(normalized);
      const from = panel.from; panel = null; go(from);
    } else { await bridge.request("overrideFilename", {taskId: panel.mediaId, filename}); const from = panel.from; panel = null; go(from); }
  }
  q(".apply-filename").addEventListener("click", () => applyFilename(false).catch(report)); q(".clear-filename").addEventListener("click", () => applyFilename(true).catch(report));
  function renderMetadata() {
    const tree = q(".metadata-tree"); tree.replaceChildren(); q(".copy-json-value").disabled = true;
    function item(key, value, path) {
      if (q(".hide-null").checked && value === null) return null;
      if (value && typeof value === "object") {
        const details = document.createElement("details"), title = document.createElement("summary"); title.textContent = key; details.append(title);
        for (const [child, entry] of Object.entries(value)) { const element = item(child, entry, path + '/' + child.replaceAll('~', '~0').replaceAll('/', '~1')); if (element) details.append(element); } return details;
      }
      const button = document.createElement("button"); button.type = "button"; button.className = "metadata-value"; button.textContent = key + ": " + JSON.stringify(value);
      button.addEventListener("click", () => { all(".metadata-value").forEach(other => other.setAttribute("aria-pressed", String(other === button))); panel.copyPath = path; q(".copy-json-value").disabled = false; }); return button;
    }
    for (const [key, value] of Object.entries(panel.details.metadata)) { const element = item(key, value, '/' + key.replaceAll('~', '~0').replaceAll('/', '~1')); if (element) tree.append(element); }
  }
  q(".hide-null").addEventListener("change", renderMetadata);
  q(".copy-json-value").addEventListener("click", () => copyClipboard({kind: 'metadata', mediaId: panel.mediaId, path: panel.copyPath}).catch(report));
  all('[data-export]').forEach(button => button.addEventListener('click', async () => { try { const result = await bridge.request('exportMetadata', {mediaId: panel.mediaId, format: button.dataset.export}); if (!result.cancelled) say(t('export_saved')); } catch (error) { report(error); } }));
  function boundedLog(text) {
    const bytes = logEncoder.encode(text), capacity = 1024 * 1024;
    if (bytes.length <= capacity) return text;
    let offset = bytes.length - capacity;
    const newline = bytes.indexOf(10, offset);
    if (newline >= 0 && newline + 1 < bytes.length) offset = newline + 1;
    else while (offset < bytes.length && (bytes[offset] & 0xc0) === 0x80) ++offset;
    return logDecoder.decode(bytes.subarray(offset));
  }
  async function loadLog(id) {
    if (logRequests.has(id)) return logRequests.get(id).promise;
    const request = {revision: -1, executionId: state.tasks.get(id)?.executionId};
    request.promise = (async () => {
      try {
        const value = await bridge.request('loadTaskLog', {taskId: id});
        const current = state.tasks.get(id), wanted = id === sidebarTaskId() || state.view === 'detail' && panel?.kind === 'logs' && panel.mediaId === id;
        if (current?.executionId === value.executionId && wanted) { state.logs.set(id, {...value, text: boundedLog(value.text)}); renderLog(); if (sidebarTaskId() === id) renderInspector(); }
        request.again = !!current && wanted && (current.executionId !== value.executionId || request.executionId === value.executionId && request.revision > value.revision);
      } finally { logRequests.delete(id); }
      if (request.again) return loadLog(id);
    })();
    logRequests.set(id, request); return request.promise;
  }
  function rememberLogView() {
    if (panel?.kind !== 'logs' || state.view !== 'detail') return;
    const scroll = detailScroll;
    if (scroll.dataset.kind !== 'logs' || q('.full-log').dataset.identity !== panel.mediaId + ':' + state.logs.get(panel.mediaId)?.executionId) return;
    lastLogView = {taskId: panel.mediaId, executionId: state.logs.get(panel.mediaId)?.executionId, top: scroll.scrollTop, atEnd: scroll.scrollTop + scroll.clientHeight >= scroll.scrollHeight - 100};
  }
  detailScroll.addEventListener('scroll', rememberLogView);
  function renderLog() {
    if (panel?.kind !== "logs") return;
    const log = state.logs.get(panel.mediaId); if (!log) return;
    const output = q(".full-log"), scroll = detailScroll, atEnd = scroll.scrollTop + scroll.clientHeight >= scroll.scrollHeight - 100, reading = window.getSelection()?.toString(), scrollTop = scroll.scrollTop;
    const identity = panel.mediaId + ':' + log.executionId, changed = output.dataset.identity !== identity;
    if (!changed && reading && output.contains(window.getSelection().anchorNode)) return;
    const saved = changed && lastLogView?.taskId === panel.mediaId && lastLogView.executionId === log.executionId ? lastLogView : null;
    const version = [identity, log.revision, log.text.length, q('.log-buffer').checked, q('.log-highlight').checked].join(':');
    if (output.dataset.version === version) return; output.dataset.version = version;
    output.dataset.identity = identity;
    const lines = log.text.split(/\r\n|\r|\n/); if (lines.at(-1) === '') lines.pop();
    const buffered = q('.log-buffer').checked && lines.length > 500;
    q('.log-limit-state').textContent = [log.truncated ? t('log_core_truncated') : '', buffered ? t('log_ui_truncated') : ''].filter(Boolean).join(' ');
    output.replaceChildren();
    for (const line of buffered ? lines.slice(-500) : lines) { const span = document.createElement("span"); span.className = /ERROR|WARNING/i.test(line) ? "log-error" : line.startsWith("[") ? "log-phase" : ""; span.textContent = line + "\n"; output.append(span); }
    output.classList.toggle("is-highlighted", q(".log-highlight").checked); q(".download-command").value = log.command;
    scroll.scrollTop = (changed ? saved?.atEnd !== false : atEnd && !reading) ? scroll.scrollHeight : saved?.top ?? scrollTop;
    rememberLogView();
  }
  bridge.on("taskLog", value => {
    if (!(panel?.kind === 'logs' && panel.mediaId === value.taskId) && sidebarTaskId() !== value.taskId) return;
    if (state.tasks.get(value.taskId)?.executionId !== value.executionId) return;
    const request = logRequests.get(value.taskId); if (request) { request.executionId = value.executionId; request.revision = Math.max(request.revision, value.revision); return; }
    const previous = state.logs.get(value.taskId);
    if (previous && previous.executionId === value.executionId && previous.revision >= value.revision) return;
    if (!previous || previous.executionId !== value.executionId || previous.revision + 1 !== value.revision) { loadLog(value.taskId).catch(report); return; }
    state.logs.set(value.taskId, {...value, text: boundedLog(previous.text + value.text)}); renderLog(); if (sidebarTaskId() === value.taskId) renderInspector();
  });
  bridge.on("metadataRefreshed", task => { updateTask(task); say(t("metadata_refreshed")); });
  bridge.on("metadataRefreshFailed", error => report(error));
  for (const selector of [".log-highlight", ".log-buffer"]) q(selector).addEventListener("change", renderLog);
  document.addEventListener('selectionchange', () => { if (!window.getSelection()?.toString()) renderLog(); });
  q(".copy-command").addEventListener("click", () => copyClipboard({kind: 'command', taskId: panel.mediaId}).catch(report));
  q('.copy-log-selection').addEventListener('mousedown', event => event.preventDefault());
  q(".copy-log-selection").addEventListener("click", () => copyClipboard({kind: 'log', taskId: panel.mediaId, selection: window.getSelection()?.toString() || ""}).catch(report));
  product.addEventListener('keydown', event => {
    if (event.target.closest('.anchored-menu')) return;
    const textFocus = event.target.closest('textarea,select,input:not([type="checkbox"]):not([type="radio"]),[contenteditable="true"]');
    if (textFocus || event.altKey || event.metaKey) return;
    const key = event.key.toLowerCase();
    const queueFocus = state.view === 'queue' && event.target.closest('.jobs');
    if (event.ctrlKey && !event.shiftKey && key === 'v' && (queueFocus || state.view === 'source')) {
      event.preventDefault(); q('.paste-link').click(); return;
    }
    if (event.ctrlKey && key === 'c' && state.view === 'detail' && panel?.kind === 'logs' && window.getSelection()?.toString() && q('.full-log').contains(window.getSelection().anchorNode)) {
      event.preventDefault(); copyClipboard({kind: 'log', taskId: panel.mediaId, selection: window.getSelection().toString()}).catch(report); return;
    }
    if (key === 'f2' && !event.ctrlKey && !event.shiftKey && state.view === 'detail' && panel?.kind === 'summary' && model(panel.mediaId)?.actions?.edit) { event.preventDefault(); openPanel('filename').catch(report); return; }
    if (!queueFocus) return;
    const ids = visibleIds();
    const visibleSelection = state.picked.size && [...state.picked].every(id => ids.includes(id));
    if (event.ctrlKey && !event.shiftKey && key === 'a') { event.preventDefault(); state.picked.clear(); q('.select-jobs').checked = true; q('.select-jobs').dispatchEvent(new Event('change')); q('.jobs').focus({preventScroll: true}); }
    else if (event.ctrlKey && !event.shiftKey && key === 'c' && visibleSelection) { event.preventDefault(); q('.batch-bar [data-batch="copy"]').click(); }
    else if (key === 'delete' && !event.ctrlKey && !event.shiftKey && visibleSelection) { event.preventDefault(); q('.batch-bar [data-batch="remove"]').click(); }
    else if (key === 'f2' && !event.ctrlKey && !event.shiftKey && ids.includes(state.selectedTask) && state.picked.size <= 1 && !q('.job-menu [data-panel="filename"]').disabled) { event.preventDefault(); openPanel('filename').catch(report); }
    else if (['arrowup', 'arrowdown', 'home', 'end'].includes(key) && ids.length) {
      event.preventDefault(); const index = ids.indexOf(state.selectedTask), next = key === 'home' ? 0 : key === 'end' ? ids.length - 1 : Math.max(0, Math.min(ids.length - 1, index + (key === 'arrowup' ? -1 : 1)));
      selectJob(ids[next], event); const row = state.rows.get(ids[next]); row?.focus({preventScroll: true});
      const list = q('.jobs'); if (row) { if (row.offsetTop < list.scrollTop) list.scrollTop = row.offsetTop; else if (row.offsetTop + row.offsetHeight > list.scrollTop + list.clientHeight) list.scrollTop = row.offsetTop + row.offsetHeight - list.clientHeight; }
    } else if (!event.ctrlKey && (key === 'contextmenu' || key === 'f10' && event.shiftKey) && ids.includes(state.selectedTask)) { event.preventDefault(); openJobMenu(); q('.job-menu button:not(:disabled)')?.focus(); }
  });
  const initialized = await bridge.request("initialize").catch(report);
  if (!initialized) { all("button").forEach(button => button.disabled = true); return; }
  strings = initialized.strings; document.documentElement.lang = initialized.language;
  all('[data-about-text]').forEach(element => element.textContent = t('about.' + element.dataset.aboutText));
  for (const [keys, label] of [['Ctrl+A', 'select'], ['Ctrl+C', 'copy'], ['Ctrl+V', 'paste'], ['Delete', 'remove'], ['F2', 'rename'], ['Apps / Shift+F10', 'menu'], ['↑ / ↓ / Home / End', 'navigate'], ['Shift+↑ / Shift+↓', 'extend'], ['Tab / Shift+Tab', 'tab'], ['Esc', 'escape']]) { const dt = document.createElement('dt'), dd = document.createElement('dd'); dt.textContent = keys; dd.textContent = t('about.key.' + label); q('.about-shortcuts').append(dt, dd); }
  pasteSource.textContent = t('paste_links'); pasteSource.title = t('paste_links') + ' (Ctrl+V)';
  for (const [selector, keys] of [['.batch-bar [data-batch="copy"]', 'Ctrl+C'], ['.batch-bar [data-batch="remove"]', 'Delete'], ['.job-menu [data-panel="filename"]', 'F2'], ['.copy-log-selection', 'Ctrl+C']]) q(selector).title = keys;
  sourceAvailability.replaceChildren(...['all', 'available', 'unavailable'].map(value => new Option(t('source_filter_' + value), value))); sourceAvailability.setAttribute('aria-label', t('source_filter'));
  q('.channel-tab').append(new Option(t('channel_podcasts'), 'podcasts'));
  for (const {top, bottom} of scrollControls) for (const [button, direction] of [[top, 'top'], [bottom, 'bottom']]) { button.title = t('scroll_' + direction); button.setAttribute('aria-label', button.title); }
  fullInfo.textContent = t('panel.summary'); fullInfo.title = fullInfo.textContent; fullInfo.setAttribute('aria-label', fullInfo.textContent);
  previewSummary.textContent = t('panel.preview'); previewSummary.title = previewSummary.textContent;
  for (const [key, label] of Object.entries({title: 'task_title', filename: 'task_filename', path: 'task_path', output: 'task_output', flags: 'preview_flags', arguments: 'preview_arguments', quality: 'context.quality', scope: 'context.scope', validation: 'context.validation'})) q('[data-preview-label="' + key + '"]').textContent = t(label);
  for (const [key, label] of Object.entries({quality: 'context.quality', scope: 'context.scope', arguments: 'preview_arguments'})) q('[data-context-label="' + key + '"]').textContent = t(label);
  state.settings = initialized.settings; state.desktop = initialized.desktop; processingDefaults = initialized.processingDefaults;
  components = new Map(initialized.components.map(component => [component.component, component]));
  componentUpdates = new Map(initialized.componentUpdates.map(update => [update.component, update]));
  all('[data-update-text]').forEach(element => element.textContent = t('update.' + element.dataset.updateText));
  for (const [selector, key] of [['.release-component', 'component'], ['.release-version', 'version']]) q(selector).setAttribute('aria-label', t('update.' + key));
  all('[data-component-text]').forEach(element => element.textContent = t('component.' + element.dataset.componentText));
  for (const [selector, key] of [['.ytdlp-path', 'ytdlp_path'], ['.ffmpeg-path', 'ffmpeg_path'], ['.update-channel', 'channel']]) q(selector).setAttribute('aria-label', t('component.' + key));
  for (const selector of ['.source-state', '.settings-save-state', '.section-error']) new MutationObserver(renderContext).observe(q(selector), {childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ['role']});
  state.queueWritable = !initialized.persistence.enabled || initialized.persistence.writable;
  persistenceError = initialized.persistence?.error || initialized.settingsPersistence?.error || null;
  document.title = t("title");
  const textKeys = await fetch("v1-text-keys.json").then(response => response.json());
  const walker = document.createTreeWalker(product, NodeFilter.SHOW_TEXT);
  while (walker.nextNode()) {
    const node = walker.currentNode, text = node.nodeValue.trim();
    if (textKeys[text]) node.nodeValue = node.nodeValue.replace(text, strings[textKeys[text]]);
  }
  all("[aria-label],[placeholder],[title]").forEach(element => {
    for (const attribute of ["aria-label", "placeholder", "title"]) {
      const key = textKeys[element.getAttribute(attribute)]; if (key) element.setAttribute(attribute, strings[key]);
    }
  });
  q('.queue-scope').replaceChildren(...['all', 'group'].map(scope => new Option(t('scope_' + scope), scope)));
  for (const button of all('[data-group-label]')) button.textContent = t(button.dataset.groupLabel);
  all('.settings-groups input, .settings-groups select, .settings-groups textarea, .settings-groups button').forEach(control => { if (!control.matches(".edit-defaults")) { control.disabled = true; control.title = t("not_migrated"); } });
  all('[data-category="about"] button').forEach(button => { button.disabled = false; button.title = ''; });
  all('[data-settings]').forEach(control => { control.disabled = !["appearance", "queue", "network", "defaults", "presets", "updates", "about"].includes(control.dataset.settings); control.setAttribute("aria-pressed", String(control.dataset.settings === "defaults")); });
  all('[data-settings-jump="updates"]').forEach(control => { control.disabled = false; control.title = ''; });
  all('.settings-category').forEach(category => category.hidden = category.dataset.category !== "defaults");
  q('.language-select').replaceChildren(new Option(t("language_zh"), "zh-CN"), new Option(t("language_en"), "en-US"));
  q('.theme-mode').replaceChildren(...['system', 'light', 'dark'].map(value => new Option(t('theme_' + value), value)));
  q('.cookie-browser-select').replaceChildren(...state.settings.capabilities.cookieBrowsers.map(name => new Option(name, name)));
  q('[data-pref="maxDownloads"]').closest("label").querySelector("small").textContent = t("download_concurrency_scope");
  q('[data-pref="lengthy"]').closest("label").title = t("processing_next_scope");
  for (const kind of ["mark", "remove"]) {
    const container = q('.sponsor-' + kind);
    for (const name of state.settings.capabilities[kind === "mark" ? "sponsorMarkCategories" : "sponsorRemoveCategories"]) {
      const label = document.createElement("label"), input = document.createElement("input"); label.className = "check-row cursor-interaction";
      input.type = "checkbox"; input.value = name; label.title = t("sponsor_description." + name); label.append(input, document.createTextNode(t("sponsor." + name))); container.append(label);
      input.addEventListener("change", () => saveApplicationField("contentProcessing", kind + "Categories", all('.sponsor-' + kind + ' input:checked').map(item => item.value)).catch(report));
    }
  }
  for (const field of state.settings.naming.fields) q('[data-name-field="' + field.id + '"]').textContent = field.label;
  const resetBandcamp = document.createElement('button'); resetBandcamp.type = 'button'; resetBandcamp.className = 'text-button reset-bandcamp cursor-interaction'; resetBandcamp.textContent = t('reset_bandcamp');
  resetBandcamp.addEventListener('click', () => { q('.bandcamp-template').value = state.settings.naming.bandcampDefault; }); q('.bandcamp-naming').after(resetBandcamp);
  q('.config-editor > .small-note').textContent = t('config_preset_scope'); q('.config-name').disabled = false;
  all('[data-name-preset]').forEach(button => button.addEventListener("click", () => previewNaming({outputTemplate: namingTemplate(button.dataset.namePreset)})));
  all("[data-batch]").filter(button => !["start", "stop", "remove", "clearDone", "copy", "skip", "unskip", "toggleSkip", "refresh"].includes(button.dataset.batch)).forEach(button => { button.disabled = true; button.title = t("not_migrated"); });
  all(".job-menu [data-task]").filter(button => !["up", "down", "file", "folder", "refresh", "split", "copy", "skip"].includes(button.dataset.task)).forEach(button => { button.disabled = true; button.title = t("not_migrated"); });
  all(".advanced-settings, .output-advanced, .naming-actions, .queue-batch, .current-filename, .raw-format-state, .single-video-hint, .window-actions").forEach(element => element.hidden = true);
  // 只开放已接通 Core 的格式与命名选择。
  renderSettings();
  for (const [key, values] of Object.entries({videoCodec: ["none", "av01", "vp9.2", "vp9", "h265", "h264", "vp8", "h263", "theora"], audioCodec: ["none", "flac", "alac", "wav", "aiff", "opus", "vorbis", "aac", "mp4a", "mp3", "ac4", "eac3", "ac3", "dts"]})) q('[data-choice="' + key + '"]').replaceChildren(...values.map(value => new Option(value === "none" ? t("no_preference") : value, value)));
  for (const selector of [".retry-select", ".concurrency-select"]) q(selector).prepend(new Option(t("keep_existing_arguments"), "keep"));
  q('[data-extra="rateUnit"]').options[0].value = "K"; q('[data-extra="rateUnit"]').options[1].value = "M";
  q(".quality-select").replaceChildren(new Option(t("no_preference"), "none"), ...[4320,2160,1440,1080,720,480,360,240,144].map(value => new Option(value + "p", String(value))));
  q(".naming-select").replaceChildren(...["title", "author", "date", "custom"].map(key => new Option(t("naming." + key), key)));
  q(".preset-select").value = "mp3";
  q(".link-box textarea").value = "";
  q(".link-box textarea").placeholder = t("multi_sources_url");
  q(".source-state").textContent = t("collection_sources");
  q(".channel-tab").closest("label").hidden = true;
  q(".argument-summary").textContent = t("backend_defaults");
  q(".desktop-footer").lastElementChild.textContent = t("update.phase");
  showQueue(initialized.queue);
  loadOptions(state.settings.newTaskOptions);
  all('[data-workbench-text]').forEach(element => element.textContent = t('workbench.' + element.dataset.workbenchText));
  all('[data-polish-text]').forEach(element => element.textContent = t('polish.' + element.dataset.polishText));
  all('[data-workbench-label]').forEach(element => element.setAttribute('aria-label', t('workbench.' + element.dataset.workbenchLabel)));
  all('[data-workbench-placeholder]').forEach(element => element.placeholder = t('workbench.' + element.dataset.workbenchPlaceholder));
  for (const button of all('.nav-button')) { button.setAttribute('aria-label', t('workbench.' + button.dataset.go)); button.title = button.getAttribute('aria-label'); }
  q('[data-page="source"] h1').textContent = t('workbench.source_title'); q('[data-page="output"] h1').textContent = t('workbench.output_title');
  q('.download-button').textContent = t('workbench.enqueue_start');
  renderSource(); renderTasks(); go("home"); say(t("ready"));
  if (initialized.initialUrl) { q('.link-box textarea').value = initialized.initialUrl; go('source'); }
  receiveCompletion(initialized.completion);
  if (state.desktop.windowPersistence.error) say(t('host_settings_failed'), true);
  lucide.createIcons({attrs: {width: 16, height: 16}});
  await bridge.request("uiReady").catch(report);
})();
