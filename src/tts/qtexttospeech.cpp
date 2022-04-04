/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Qt Speech module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/



#include "qtexttospeech.h"
#include "qtexttospeech_p.h"

#include <qdebug.h>

#include <QtCore/private/qfactoryloader_p.h>

QT_BEGIN_NAMESPACE

Q_GLOBAL_STATIC_WITH_ARGS(QFactoryLoader, loader,
        ("org.qt-project.qt.speech.tts.plugin/5.0",
         QLatin1String("/texttospeech")))

QMutex QTextToSpeechPrivate::m_mutex;

QTextToSpeechPrivate::QTextToSpeechPrivate(QTextToSpeech *speech)
    : q_ptr(speech)
{
    qRegisterMetaType<QTextToSpeech::State>();
}

QTextToSpeechPrivate::~QTextToSpeechPrivate()
{
    delete m_engine;
}

void QTextToSpeechPrivate::setEngineProvider(const QString &engine)
{
    Q_Q(QTextToSpeech);

    q->stop();
    delete m_engine;

    m_providerName = engine;
    if (m_providerName.isEmpty()) {
        const auto plugins = QTextToSpeechPrivate::plugins();
        int priority = -1;
        for (const auto &&[provider, metadata] : plugins.asKeyValueRange()) {
            const int pluginPriority = metadata.value(QStringLiteral("Priority")).toInteger();
            if (pluginPriority > priority) {
                priority = pluginPriority;
                m_providerName = provider;
            }
        }
        if (m_providerName.isEmpty()) {
            qCritical() << "No text-to-speech plug-ins were found.";
            return;
        }
    }
    if (!loadMeta()) {
        qCritical() << "Text-to-speech plug-in" << m_providerName << "is not supported.";
        return;
    }
    loadPlugin();
    if (m_plugin) {
        QString errorString;
        m_engine = m_plugin->createTextToSpeechEngine(QVariantMap(), 0, &errorString);
        if (!m_engine) {
            qCritical() << "Error creating text-to-speech engine" << m_providerName
                        << (errorString.isEmpty() ? QStringLiteral("") : (QStringLiteral(": ") + errorString));
        } else {
            m_engine->setProperty("providerName", m_providerName);
        }
        m_engine->setProperty("providerName", m_providerName);
    } else {
        qCritical() << "Error loading text-to-speech plug-in" << m_providerName;
    }

    // Connect state change signal directly from the engine to the public API signal
    if (m_engine)
        QObject::connect(m_engine, &QTextToSpeechEngine::stateChanged, q, &QTextToSpeech::stateChanged);
}

bool QTextToSpeechPrivate::loadMeta()
{
    m_plugin = nullptr;
    m_metaData = QCborMap();

    QList<QCborMap> candidates = QTextToSpeechPrivate::plugins().values(m_providerName);

    int versionFound = -1;

    // figure out which version of the plugin we want
    for (int i = 0; i < candidates.size(); ++i) {
        QCborMap meta = candidates[i];
        if (int ver = meta.value(QLatin1String("Version")).toInteger(); ver > versionFound) {
            versionFound = ver;
            m_metaData = std::move(meta);
        }
    }

    if (m_metaData.isEmpty()) {
        m_metaData.insert(QLatin1String("index"), -1); // not found
        return false;
    }

    return true;
}

void QTextToSpeechPrivate::loadPlugin()
{
    int idx = m_metaData.value(QLatin1String("index")).toInteger();
    if (idx < 0) {
        m_plugin = nullptr;
        return;
    }
    m_plugin = qobject_cast<QTextToSpeechPlugin *>(loader()->instance(idx));
}

QMultiHash<QString, QCborMap> QTextToSpeechPrivate::plugins(bool reload)
{
    static QMultiHash<QString, QCborMap> plugins;
    static bool alreadyDiscovered = false;
    QMutexLocker lock(&m_mutex);

    if (reload == true)
        alreadyDiscovered = false;

    if (!alreadyDiscovered) {
        loadPluginMetadata(plugins);
        alreadyDiscovered = true;
    }
    return plugins;
}

void QTextToSpeechPrivate::loadPluginMetadata(QMultiHash<QString, QCborMap> &list)
{
    QFactoryLoader *l = loader();
    QList<QPluginParsedMetaData> meta = l->metaData();
    for (int i = 0; i < meta.size(); ++i) {
        QCborMap obj = meta.at(i).value(QtPluginMetaDataKeys::MetaData).toMap();
        obj.insert(QLatin1String("index"), i);
        list.insert(obj.value(QLatin1String("Provider")).toString(), obj);
    }
}

/*!
  \class QTextToSpeech
  \brief The QTextToSpeech class provides a convenient access to text-to-speech engines.
  \inmodule QtSpeech

  Use \l say() to start synthesizing text.
  It is possible to specify the language with \l setLocale().
  To select between the available voices use \l setVoice().
  The languages and voices depend on the available synthesizers on each platform.
  On Linux, \c speech-dispatcher is used by default.
*/

/*!
  \enum QTextToSpeech::State
  \value Ready          The synthesizer is ready to start a new text. This is
                        also the state after a text was finished.
  \value Speaking       The current text is being spoken.
  \value Paused         The synthesis was paused and can be resumed with \l resume().
  \value BackendError   The backend was unable to synthesize the current string.
*/

/*!
  \property QTextToSpeech::state
  This property holds the current state of the speech synthesizer.
  Use \l say() to start synthesizing text with the current voice and locale.

*/

/*!
    Loads a text-to-speech engine from a plug-in that uses the default
    engine plug-in and constructs a QTextToSpeech object as the child
    of \a parent.

    The default engine may be platform-specific.

    If the plugin fails to load, QTextToSpeech::state() returns
    QTextToSpeech::BackendError.

    \sa availableEngines()
*/
QTextToSpeech::QTextToSpeech(QObject *parent)
    : QObject(*new QTextToSpeechPrivate(this), parent)
{
    Q_D(QTextToSpeech);
    d->setEngineProvider(QString());
}

/*!
  Loads a text-to-speech engine from a plug-in that matches parameter \a engine and
  constructs a QTextToSpeech object as the child of \a parent.

  If \a engine is empty, the default engine plug-in is used. The default
  engine may be platform-specific.

  If the plugin fails to load, QTextToSpeech::state() returns QTextToSpeech::BackendError.

  \sa availableEngines()
*/
QTextToSpeech::QTextToSpeech(const QString &engine, QObject *parent)
    : QObject(*new QTextToSpeechPrivate(this), parent)
{
    Q_D(QTextToSpeech);
    d->setEngineProvider(engine);
}

/*!
  Destroys this QTextToSpeech object, stopping any speech.
*/
QTextToSpeech::~QTextToSpeech()
{
    stop();
}

/*!
    \property QTextToSpeech::engine
    \brief the engine used to synthesize text to speech.

    Changing the engine stops any ongoing speech.
*/
bool QTextToSpeech::setEngine(const QString &engine)
{
    Q_D(QTextToSpeech);
    if (d->m_providerName == engine)
        return true;

    d->setEngineProvider(engine);

    emit engineChanged(d->m_providerName);
    return d->m_engine;
}

QString QTextToSpeech::engine() const
{
    Q_D(const QTextToSpeech);
    return d->m_providerName;
}

/*!
 Gets the list of supported text-to-speech engine plug-ins.
*/
QStringList QTextToSpeech::availableEngines()
{
    return QTextToSpeechPrivate::plugins().keys();
}

QTextToSpeech::State QTextToSpeech::state() const
{
    Q_D(const QTextToSpeech);
    if (d->m_engine)
        return d->m_engine->state();
    return QTextToSpeech::BackendError;
}

/*!
  Start synthesizing the \a text.
  This function will start the asynchronous reading of the text.
  The current state is available using the \l state property. Once the
  synthesis is done, a \l stateChanged() signal with the \l Ready state
  is emitted.
*/
void QTextToSpeech::say(const QString &text)
{
    Q_D(QTextToSpeech);
    if (d->m_engine)
        d->m_engine->say(text);
}

/*!
  Stop the text that is being read.
*/
void QTextToSpeech::stop()
{
    Q_D(QTextToSpeech);
    if (d->m_engine)
        d->m_engine->stop();
}

/*!
  Pauses the current speech.

  Note:
  \list
      \li This function depends on the platform and the backend. It may not
      work at all, it may take several seconds before it takes effect,
      or it may pause instantly.
      Some synthesizers will look for a break that they can later resume
      from, such as a sentence end.
      \li Due to Android platform limitations, pause() stops what is presently
      being said, while resume() starts the previously queued sentence from
      the beginning.
  \endlist

  \sa resume()
*/
void QTextToSpeech::pause()
{
    Q_D(QTextToSpeech);
    if (d->m_engine)
        d->m_engine->pause();
}

/*!
  Resume speaking after \l pause() has been called.
  \sa pause()
*/
void QTextToSpeech::resume()
{
    Q_D(QTextToSpeech);
    if (d->m_engine)
        d->m_engine->resume();
}

/*!
 \property QTextToSpeech::pitch
 This property holds the voice pitch, ranging from -1.0 to 1.0.
 The default of 0.0 is the normal speech pitch.
*/

void QTextToSpeech::setPitch(double pitch)
{
    Q_D(QTextToSpeech);
    if (d->m_engine && d->m_engine->setPitch(pitch))
        emit pitchChanged(pitch);
}

double QTextToSpeech::pitch() const
{
    Q_D(const QTextToSpeech);
    if (d->m_engine)
        return d->m_engine->pitch();
    return 0.0;
}

/*!
 \property QTextToSpeech::rate
 This property holds the current voice rate, ranging from -1.0 to 1.0.
 The default value of 0.0 is normal speech flow.
*/
void QTextToSpeech::setRate(double rate)
{
    Q_D(QTextToSpeech);
    if (d->m_engine && d->m_engine->setRate(rate))
        emit rateChanged(rate);
}

double QTextToSpeech::rate() const
{
    Q_D(const QTextToSpeech);
    if (d->m_engine)
        return d->m_engine->rate();
    return 0.0;
}

/*!
 \property QTextToSpeech::volume
 This property holds the current volume, ranging from 0.0 to 1.0.
 The default value is the platform's default volume.
*/
void QTextToSpeech::setVolume(double volume)
{
    Q_D(QTextToSpeech);
    volume = qBound(0.0, volume, 1.0);
    if (d->m_engine && d->m_engine->setVolume(volume))
        emit volumeChanged(volume);
}

double QTextToSpeech::volume() const
{
    Q_D(const QTextToSpeech);
    if (d->m_engine)
        return d->m_engine->volume();
    return 0.0;
}

/*!
 Sets the \a locale to a given locale if possible.
 The default is the system locale.
*/
void QTextToSpeech::setLocale(const QLocale &locale)
{
    Q_D(QTextToSpeech);
    if (d->m_engine && d->m_engine->setLocale(locale)) {
        emit localeChanged(locale);
        emit voiceChanged(d->m_engine->voice());
    }
}

/*!
 \property QTextToSpeech::locale
 This property holds the current locale in use. By default, the system locale
 is used.
*/
QLocale QTextToSpeech::locale() const
{
    Q_D(const QTextToSpeech);
    if (d->m_engine)
        return d->m_engine->locale();
    return QLocale();
}

/*!
 Gets a list of locales that are currently supported.
 \note On some platforms these can change, for example,
       when the backend changes synthesizers.
*/
QList<QLocale> QTextToSpeech::availableLocales() const
{
    Q_D(const QTextToSpeech);
    if (d->m_engine)
        return d->m_engine->availableLocales();
    return QList<QLocale>();
}

/*!
 Sets the \a voice to use.

 \note On some platforms, setting the voice changes other voice attributes
 such as locale, pitch, and so on. These changes trigger the emission of signals.
*/
void QTextToSpeech::setVoice(const QVoice &voice)
{
    Q_D(QTextToSpeech);
    if (d->m_engine && d->m_engine->setVoice(voice))
        emit voiceChanged(voice);
}

/*!
 \property QTextToSpeech::voice
 This property holds the current voice used for the speech.
*/
QVoice QTextToSpeech::voice() const
{
    Q_D(const QTextToSpeech);
    if (d->m_engine)
        return d->m_engine->voice();
    return QVoice();
}

/*!
 Gets a list of voices available for the current locale.
 \note if no locale has been set, the system locale is used.
*/
QList<QVoice> QTextToSpeech::availableVoices() const
{
    Q_D(const QTextToSpeech);
    if (d->m_engine)
        return d->m_engine->availableVoices();
    return QList<QVoice>();
}

QT_END_NAMESPACE
