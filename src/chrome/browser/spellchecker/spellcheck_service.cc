// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/spellchecker/spellcheck_service.h"

#include "base/logging.h"
#include "base/prefs/pref_member.h"
#include "base/prefs/pref_service.h"
#include "base/strings/string_split.h"
#include "base/supports_user_data.h"
#include "base/synchronization/waitable_event.h"
#include "chrome/browser/spellchecker/feedback_sender.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/spellchecker/spellcheck_host_metrics.h"
#include "chrome/browser/spellchecker/spellcheck_hunspell_dictionary.h"
#include "chrome/browser/spellchecker/spellcheck_platform_mac.h"
#include "chrome/browser/spellchecker/spelling_service_client.h"
#include "chrome/common/pref_names.h"
#include "chrome/common/spellcheck_common.h"
#include "chrome/common/spellcheck_messages.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "ipc/ipc_platform_file.h"

using content::BrowserThread;
using chrome::spellcheck_common::FileLanguagePair;

// TODO(rlp): I do not like globals, but keeping these for now during
// transition.
// An event used by browser tests to receive status events from this class and
// its derived classes.
base::WaitableEvent* g_status_event = NULL;
SpellcheckService::EventType g_status_type =
    SpellcheckService::BDICT_NOTINITIALIZED;

SpellcheckService::SpellcheckService(content::BrowserContext* context)
    : context_(context),
      weak_ptr_factory_(this) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PrefService* prefs = user_prefs::UserPrefs::Get(context);
  pref_change_registrar_.Init(prefs);
  StringListPrefMember dictionaries_pref;
  dictionaries_pref.Init(prefs::kSpellCheckDictionaries, prefs);
  std::string first_of_dictionaries;
  if (!dictionaries_pref.GetValue().empty())
    first_of_dictionaries = dictionaries_pref.GetValue().front();

  // For preference migration, set the new preference kSpellCheckDictionaries
  // to be the same as the old kSpellCheckDictionary.
  StringPrefMember single_dictionary_pref;
  single_dictionary_pref.Init(prefs::kSpellCheckDictionary, prefs);
  std::string single_dictionary = single_dictionary_pref.GetValue();

  if (first_of_dictionaries.empty() && !single_dictionary.empty()) {
    first_of_dictionaries = single_dictionary;
    dictionaries_pref.SetValue(
        std::vector<std::string>(1, first_of_dictionaries));
  }

  single_dictionary_pref.SetValue("");

  std::string language_code;
  std::string country_code;
  chrome::spellcheck_common::GetISOLanguageCountryCodeFromLocale(
      first_of_dictionaries,
      &language_code,
      &country_code);

  // SHEZ: Remove feedback sender
  // feedback_sender_.reset(new spellcheck::FeedbackSender(
  //     context->GetRequestContext(), language_code, country_code));

  pref_change_registrar_.Add(
      prefs::kEnableAutoSpellCorrect,
      base::Bind(&SpellcheckService::OnEnableAutoSpellCorrectChanged,
                 base::Unretained(this)));
  pref_change_registrar_.Add(
      prefs::kSpellCheckDictionaries,
      base::Bind(&SpellcheckService::OnSpellCheckDictionariesChanged,
                 base::Unretained(this)));
  if (!chrome::spellcheck_common::IsMultilingualSpellcheckEnabled()) {
    pref_change_registrar_.Add(
        prefs::kSpellCheckUseSpellingService,
        base::Bind(&SpellcheckService::OnUseSpellingServiceChanged,
                   base::Unretained(this)));
  }

  pref_change_registrar_.Add(
      prefs::kEnableContinuousSpellcheck,
      base::Bind(&SpellcheckService::InitForAllRenderers,
                 base::Unretained(this)));

  OnSpellCheckDictionariesChanged();

  content::SpellcheckData* spellcheckData =
      content::SpellcheckData::FromContext(context);
  if (spellcheckData) {
    // If the browser-context has SpellcheckData, then we will use that instead
    // of SpellcheckCustomDictionary, which reads & writes the words list to
    // disk.
    spellcheckData->AddObserver(this);
  }
  else {
    custom_dictionary_.reset(new SpellcheckCustomDictionary(context_->GetPath()));
    custom_dictionary_->AddObserver(this);
    custom_dictionary_->Load();
  }

  registrar_.Add(this,
                 content::NOTIFICATION_RENDERER_PROCESS_CREATED,
                 content::NotificationService::AllSources());
}

SpellcheckService::~SpellcheckService() {
  // Remove pref observers
  pref_change_registrar_.RemoveAll();
}

base::WeakPtr<SpellcheckService> SpellcheckService::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

#if !defined(OS_MACOSX)
// static
size_t SpellcheckService::GetSpellCheckLanguages(
    base::SupportsUserData* context,
    std::vector<std::string>* languages) {
  PrefService* prefs = user_prefs::UserPrefs::Get(context);
  StringPrefMember accept_languages_pref;
  accept_languages_pref.Init(prefs::kAcceptLanguages, prefs);

  std::vector<std::string> accept_languages;
  base::SplitString(accept_languages_pref.GetValue(), ',', &accept_languages);

  StringListPrefMember dictionaries_pref;
  dictionaries_pref.Init(prefs::kSpellCheckDictionaries, prefs);
  *languages = dictionaries_pref.GetValue();
  size_t enabled_spellcheck_languages = languages->size();

  for (std::vector<std::string>::const_iterator i = accept_languages.begin();
       i != accept_languages.end(); ++i) {
    std::string language =
        chrome::spellcheck_common::GetCorrespondingSpellCheckLanguage(*i);
    if (!language.empty() &&
        std::find(languages->begin(), languages->end(), language) ==
            languages->end()) {
      languages->push_back(language);
    }
  }

  return enabled_spellcheck_languages;
}
#endif  // !OS_MACOSX

// static
bool SpellcheckService::SignalStatusEvent(
    SpellcheckService::EventType status_type) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  if (!g_status_event)
    return false;
  g_status_type = status_type;
  g_status_event->Signal();
  return true;
}

void SpellcheckService::StartRecordingMetrics(bool spellcheck_enabled) {
  metrics_.reset(new SpellCheckHostMetrics());
  metrics_->RecordEnabledStats(spellcheck_enabled);
  OnUseSpellingServiceChanged();
}

void SpellcheckService::InitForRenderer(content::RenderProcessHost* process) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  content::BrowserContext* context = process->GetBrowserContext();
  if (SpellcheckServiceFactory::GetForContext(context) != this)
    return;

  PrefService* prefs = user_prefs::UserPrefs::Get(context);
  std::vector<FileLanguagePair> languages;

  typedef ScopedVector<SpellcheckHunspellDictionary>::iterator DictIterator;

  for (DictIterator it = hunspell_dictionaries_.begin();
      it != hunspell_dictionaries_.end();
      ++it) {
    SpellcheckHunspellDictionary *d = *it;
    IPC::PlatformFileForTransit file = IPC::InvalidPlatformFileForTransit();

    if (d->GetDictionaryFile().IsValid()) {
        file = IPC::GetFileHandleForProcess(
            d->GetDictionaryFile().GetPlatformFile(),
            process->GetHandle(), false);
    }

    languages.push_back(FileLanguagePair(file, d->GetLanguage()));
  }

  const std::set<std::string>* custom_words_ptr;

  content::SpellcheckData* spellcheckData =
      content::SpellcheckData::FromContext(context_);
  if (spellcheckData) {
    custom_words_ptr = &spellcheckData->custom_words();
  }
  else {
    DCHECK(custom_dictionary_);
    custom_words_ptr = &custom_dictionary_->GetWords();
  }

  process->Send(new SpellCheckMsg_Init(
      languages,
      *custom_words_ptr,
      prefs->GetBoolean(prefs::kEnableAutoSpellCorrect)));
  process->Send(new SpellCheckMsg_EnableSpellCheck(
      prefs->GetBoolean(prefs::kEnableContinuousSpellcheck)));
}

SpellCheckHostMetrics* SpellcheckService::GetMetrics() const {
  return metrics_.get();
}

SpellcheckCustomDictionary* SpellcheckService::GetCustomDictionary() {
  return custom_dictionary_.get();
}

bool SpellcheckService::LoadExternalDictionary(std::string language,
                                               std::string locale,
                                               std::string path,
                                               DictionaryFormat format) {
  return false;
}

bool SpellcheckService::UnloadExternalDictionary(std::string path) {
  return false;
}

void SpellcheckService::Observe(int type,
                                const content::NotificationSource& source,
                                const content::NotificationDetails& details) {
  DCHECK(type == content::NOTIFICATION_RENDERER_PROCESS_CREATED);
  content::RenderProcessHost* process =
      content::Source<content::RenderProcessHost>(source).ptr();
  InitForRenderer(process);
}

// content::SpellcheckData::Observer implementation.
void SpellcheckService::OnCustomWordsChanged(
    const std::vector<base::StringPiece>& words_added,
    const std::vector<base::StringPiece>& words_removed) {
  std::set<std::string> words_added_copy;
  std::set<std::string> words_removed_copy;
  for (size_t i = 0; i < words_added.size(); ++i) {
    std::string word;
    words_added[i].CopyToString(&word);
    words_added_copy.insert(word);
  }
  for (size_t i = 0; i < words_removed.size(); ++i) {
    std::string word;
    words_removed[i].CopyToString(&word);
    words_removed_copy.insert(word);
  }
  for (content::RenderProcessHost::iterator i(
          content::RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    content::RenderProcessHost* process = i.GetCurrentValue();
    if (!process || context_ != process->GetBrowserContext())
      continue;
    process->Send(new SpellCheckMsg_CustomDictionaryChanged(
        words_added_copy,
        words_removed_copy));
  }
}

void SpellcheckService::OnCustomDictionaryLoaded() {
  InitForAllRenderers();
}

void SpellcheckService::OnCustomDictionaryChanged(
    const SpellcheckCustomDictionary::Change& dictionary_change) {
  for (content::RenderProcessHost::iterator i(
          content::RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    i.GetCurrentValue()->Send(new SpellCheckMsg_CustomDictionaryChanged(
        dictionary_change.to_add(),
        dictionary_change.to_remove()));
  }
}

void SpellcheckService::OnHunspellDictionaryInitialized() {
  InitForAllRenderers();
}

void SpellcheckService::OnHunspellDictionaryDownloadBegin() {
}

void SpellcheckService::OnHunspellDictionaryDownloadSuccess() {
}

void SpellcheckService::OnHunspellDictionaryDownloadFailure() {
}

// static
void SpellcheckService::AttachStatusEvent(base::WaitableEvent* status_event) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  g_status_event = status_event;
}

// static
SpellcheckService::EventType SpellcheckService::GetStatusEvent() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  return g_status_type;
}

void SpellcheckService::InitForAllRenderers() {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  for (content::RenderProcessHost::iterator i(
          content::RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    content::RenderProcessHost* process = i.GetCurrentValue();
    if (process && process->GetHandle())
      InitForRenderer(process);
  }
}

void SpellcheckService::OnEnableAutoSpellCorrectChanged() {
  bool enabled = pref_change_registrar_.prefs()->GetBoolean(
      prefs::kEnableAutoSpellCorrect);
  for (content::RenderProcessHost::iterator i(
           content::RenderProcessHost::AllHostsIterator());
       !i.IsAtEnd(); i.Advance()) {
    content::RenderProcessHost* process = i.GetCurrentValue();
    process->Send(new SpellCheckMsg_EnableAutoSpellCorrect(enabled));
  }
}

void SpellcheckService::OnSpellCheckDictionariesChanged() {
  // Delete all the SpellcheckHunspellDictionary and unobserve them
  typedef ScopedVector<SpellcheckHunspellDictionary>::iterator Iterator;
  for (Iterator it = hunspell_dictionaries_.begin();
      it != hunspell_dictionaries_.end();
      ++it) {
    SpellcheckHunspellDictionary *hunspell_dictionary = *it;
    hunspell_dictionary->RemoveObserver(this);
  }
  hunspell_dictionaries_.clear();

  // Create the new vector of dictionaries
  PrefService* prefs = user_prefs::UserPrefs::Get(context_);
  DCHECK(prefs);

  // SHEZ: The upstream code currently only checks the first item in the
  // SHEZ: kSpellCheckDictionaries setting.
  // SHEZ: TODO: make it check all dictionaries, and stop using the
  // SHEZ: TODO: comma-separated string.
#if 0
  std::string dictionary;
  prefs->GetList(prefs::kSpellCheckDictionaries)->GetString(0, &dictionary);
#endif

  std::vector<std::string> languages;
  base::SplitString(prefs->GetString(prefs::kSpellCheckDictionary), ',', &languages);

  for (size_t langIndex = 0; langIndex < languages.size(); ++langIndex) {
    SpellcheckHunspellDictionary *hunspell_dictionary
        = new SpellcheckHunspellDictionary(languages[langIndex],
                                           context_->AllowDictionaryDownloads() ? context_->GetRequestContext() : 0,
                                           this);

    hunspell_dictionary->AddObserver(this);
    hunspell_dictionary->Load();
    hunspell_dictionaries_.push_back(hunspell_dictionary);
  }
}

void SpellcheckService::OnUseSpellingServiceChanged() {
  bool enabled = pref_change_registrar_.prefs()->GetBoolean(
      prefs::kSpellCheckUseSpellingService);
  if (metrics_)
    metrics_->RecordSpellingServiceStats(enabled);
  // SHEZ: Remove feedback sender
  // UpdateFeedbackSenderState();
}

// SHEZ: Remove feedback sender
// void SpellcheckService::UpdateFeedbackSenderState() {
//   if (SpellingServiceClient::IsAvailable(
//           context_, SpellingServiceClient::SPELLCHECK)) {
//     feedback_sender_->StartFeedbackCollection();
//   } else {
//     feedback_sender_->StopFeedbackCollection();
//   }
// }
