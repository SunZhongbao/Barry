#include "luaexecutethread.hpp"
#include <QtCore/QString>
#include "bhj_help.hpp"
#include <QtCore/QStringList>
#include <QtCore/QDebug>
#include <QtCore/QProcess>
#include "lua.hpp"
#include <QtWidgets/QMessageBox>
#include <QTcpSocket>
#include "wrench.h"
#include "adbclient.h"
#include <QtCore/QFileInfoList>

LuaExecuteThread* that;

static QStringList l_getArgs(lua_State* L, int n_args = 0)
{
    QStringList args;
    if (! lua_istable(L, -1)) {
        if (n_args == 0 || ! lua_isstring(L, -1)) {
            luaL_argerror(L, n_args, "wrong type of args");
            return args;
        }
        for (int i = 1; i <= n_args; i++) {
            if (! lua_isstring(L, i)) {
                luaL_argerror(L, n_args, "wrong type of args");
                return args;
            }
            args << QString::fromUtf8(lua_tolstring(L, i, NULL));
        }
        return args;
    }
    int n = luaL_len(L, -1);

    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, -1, i);
        args << (QString::fromUtf8(lua_tolstring(L, -1, NULL)));
        lua_settop(L, -2);
    }

    if (n_args != 0 && args.size() != n_args) {
        luaL_argerror(L, n_args, qPrintable(QString().sprintf("takes %d args", n_args)));
    }
    return args;
}

static int l_adb_push(lua_State* L)
{
    QStringList args = l_getArgs(L, 2);

    if (!AdbClient::doAdbPush(args[0], args[1])) {
        lua_pushstring(L, "adb push failed");
        lua_error(L);
    }
    return 0;
}

static int l_adb_pull(lua_State* L)
{
    QStringList args = l_getArgs(L, 2);

    qDebug() << "adb pull" << args;

    if (!AdbClient::doAdbPull(args[0], args[1])) {
        lua_pushstring(L, ("adb pull failed: " + args[0] + " " + args[1]).toUtf8().constData());
        lua_error(L);
    }
    return 0;
}

static int l_adb_install(lua_State* L)
{
    QStringList args = l_getArgs(L, 1);

    QString apk = args[0];
    QString tmpApk = QString("/data/local/tmp/") + QFileInfo(apk).fileName();
    qDebug() << "apk is" << apk << "tmpApk is" << tmpApk;
    if (!AdbClient::doAdbPush(apk, tmpApk)) {
        lua_pushstring(L, "adb push apk for install failed");
        lua_error(L);
    }

    QString res = AdbClient::doAdbShell("pm install -r -g " + tmpApk);
    if (res == "") {
        lua_pushstring(L, "adb install failed");
        lua_error(L);
    }

    lua_pushstring(L, res.toUtf8().constData());
    // pm 'install' '-r' '/data/local/tmp/Setclip.apk'
    return 1;
}

static int l_selectArg(lua_State* L)
{
    QStringList args = l_getArgs(L);

    QString res = that->selectArgs(args);
    lua_pushstring(L, res.toUtf8().constData());
    return 1;
}

QString LuaExecuteThread::selectArgs(const QStringList& args)
{
    emit selectArgsSig(args);
    mSelectArgsMutex.lock();
    mSelectArgsWait.wait(&mSelectArgsMutex);
    QString res = mSelectedArg;
    mSelectArgsMutex.unlock();
    return res;
}

static int l_insertText(lua_State* L)
{
    QStringList args = l_getArgs(L, 1);
    that->insertText(args[0]);
    return 0;
}

void LuaExecuteThread::insertText(const QString& text)
{
    emit insertTextSig(text);
}

static int l_wrench_get_adb_devices(lua_State* L)
{

    QStringList adbDevices = AdbClient::doAdbDevices();
    if (adbDevices.size() == 0)
        return 0;

    lua_createtable(L, adbDevices.size(), 0);
    for (int i = 0; i < adbDevices.size(); i++) {
        lua_pushinteger(L, i + 1);
        lua_pushstring(L, adbDevices[i].toUtf8().constData());
        lua_settable(L, -3);
    }
    return 1;
}

static int l_wrench_get_variable(lua_State* L)
{
    QStringList args = l_getArgs(L, 1);

    QString res = that->getVariableLocked(args[0]);
    lua_pushstring(L, res.toUtf8().constData());
    return 1;
}

static int l_wrench_get_proc_var(lua_State* L)
{
    QStringList args = l_getArgs(L, 1);

    QString res = that->getProcessVarLocked(args[0]);
    lua_pushstring(L, res.toUtf8().constData());
    return 1;
}

QHash<QString, QString> LuaExecuteThread::mProcessVarHash;

static int l_clickNotification(lua_State* L)
{
    QStringList args = l_getArgs(L);

    that->clickNotification(args);
    return 0;
}

static int l_gotUiTask(lua_State* L)
{
    QStringList args = l_getArgs(L);

    that->gotUiTask(args);
    return 0;
}

static int l_lsFiles(lua_State* L)
{
    QStringList args = l_getArgs(L, 2);

    QDir dir(args[0], args[1]);
    QFileInfoList fileInfoList = dir.entryInfoList();

    lua_createtable(L, fileInfoList.size(), 0);
    int table_index = lua_gettop(L);
    for (int i = 0; i < fileInfoList.size(); i++)
    {
        lua_pushstring(L, fileInfoList[i].absoluteFilePath().toUtf8().constData());
        lua_rawseti(L, table_index, i + 1);
    }
    return 1;
}

static int l_selectApps(lua_State* L)
{
    that->selectApps();
    return 0;
}

static int l_showNotifications(lua_State* L)
{
    that->showNotifications();
    return 0;
}

static int l_qt_adb_pipe(lua_State* L)
{
    QStringList args = l_getArgs(L);

    QString res = AdbClient::doAdbShell(args);
    lua_pushstring(L, res.toUtf8().constData());
    return 1;
}

static int l_logToUI(lua_State* L)
{
    that->logToUI(lua_tolstring(L, -1, NULL));
    return 0;
}

static int l_is_exiting(lua_State* L)
{
    lua_pushboolean(L, that->isQuit());
    return 1;
}

static int l_adbQuickInputAm(lua_State* L)
{
    int n = luaL_len(L, -1);
    if (n != 1) {
        QString error = QString().sprintf("takes only 1 string, got %d", n);
        luaL_argerror(L, 1, qPrintable(error));
    }

    QString arg;
    lua_rawgeti(L, -1, n);
    arg = QString::fromUtf8(lua_tolstring(L, -1, NULL));
    lua_settop(L, -2);

    QString res = that->adbQuickInputAm(arg);
    lua_pushstring(L, res.toUtf8().constData());
    return 1;
}

static bool wrenchSockOk(QTcpSocket* wrenchSock)
{
    if (wrenchSock == NULL) {
        return false;
    }

    wrenchSock->write("ping\n");
    wrenchSock->flush();
    wrenchSock->readLine();
    if (wrenchSock->state() != QAbstractSocket::ConnectedState) {
        return false;
    }
    return true;
}

QString LuaExecuteThread::adbQuickInputAm(QString arg)
{
    if (!wrenchSockOk(wrenchSock)) {
        if (wrenchSock) {
            delete wrenchSock;
            wrenchSock = NULL;
            qDebug() << "prepare to reconnect wrenchSock";
        }
        qDebug() << "connect to localhost 28888";
        wrenchSock = new QTcpSocket();
        wrenchSock->connectToHost("localhost", 28888 + QProcessEnvironment::systemEnvironment().value("WRENCH_INSTANCE", "0").toInt(), QIODevice::ReadWrite);
        wrenchSock->waitForConnected();
    }

    QString res;
    if (arg.startsWith("input ") || arg.startsWith("sleep ")) {
        QStringList actions = arg.split(";", QString::SkipEmptyParts);
        foreach (const QString& action, actions) {
            if (action.startsWith("sleep ")) {
                int msec = atof(qPrintable(action.mid(6))) * 1000;
                emit requestSyncScreen();
                QThread::msleep(msec);
                continue;
            }

            wrenchSock->write(action.toUtf8() + "\n");
            wrenchSock->flush();
            if (!wrenchSock->waitForReadyRead(2000)) {
                emit gotSomeLog("info", QString("命令超时： ") + action);
            }
            res = wrenchSock->readLine();
        }
    } else if (arg.startsWith("am ")) {
        wrenchSock->write(arg.toUtf8() + "\n");
        wrenchSock->flush();
        if (!wrenchSock->waitForReadyRead(2000)) {
            emit gotSomeLog("info", QString("命令超时： ") + arg);
        }
        res = wrenchSock->readLine();
    } else {
        QString error = "Invalid wrenchsock input: " + arg;
        luaL_argerror(L, 1, qPrintable(error));
    }

    return res;
}

void LuaExecuteThread::setVariableLocked(const QString& name, const QString& val)
{
    mVariableMutex.lock();
    mVariableHash[name] = val;
    mVariableMutex.unlock();
}

void LuaExecuteThread::setProcessVarLocked(const QString& name, const QString& val)
{
    mVariableMutex.lock();
    mProcessVarHash[name] = val;
    mVariableMutex.unlock();

    if (name == "ANDROID_SERIAL") {
        if (val != AdbClient::getAdbSerial()) {
            AdbClient::setAdbSerial(val);
        }
    }
}

QString LuaExecuteThread::getVariableLocked(const QString& name, const QString& defaultVal)
{
    QString res;
    mVariableMutex.lock();
    if (mVariableHash.contains(name)) {
        res = mVariableHash[name];
    } else {
        res = defaultVal;
    }
    mVariableMutex.unlock();
    return res;
}

QString LuaExecuteThread::getProcessVarLocked(const QString& name, const QString& defaultVal)
{
    QString res;
    mVariableMutex.lock();
    if (mProcessVarHash.contains(name)) {
        res = mProcessVarHash[name];
    } else {
        res = defaultVal;
    }
    mVariableMutex.unlock();
    return res;
}

static int l_wrench_set_variable(lua_State* L)
{
    QString name = QString::fromUtf8(lua_tolstring(L, 1, NULL));
    QString val = QString::fromUtf8(lua_tolstring(L, 2, NULL));
    that->setVariableLocked(name, val);
    return 0;
}

static int l_wrench_set_proc_var(lua_State* L)
{
    QString name = QString::fromUtf8(lua_tolstring(L, 1, NULL));
    QString val = QString::fromUtf8(lua_tolstring(L, 2, NULL));
    that->setProcessVarLocked(name, val);
    return 0;
}

bool LuaExecuteThread::isQuit()
{
    return mQuit;
}

void LuaExecuteThread::run()
{
    L = luaL_newstate();             /* opens Lua */
    luaL_openlibs(L);        /* opens the standard libraries */

    lua_pushcfunction(L, l_selectArg);
    lua_setglobal(L, "select_from_args_table");

    lua_pushcfunction(L, l_clickNotification);
    lua_setglobal(L, "clickNotification");

    lua_pushcfunction(L, l_gotUiTask);
    lua_setglobal(L, "gotUiTask");

    lua_pushcfunction(L, l_selectApps);
    lua_setglobal(L, "select_apps");

    lua_pushcfunction(L, l_insertText);
    lua_setglobal(L, "wrench_insert_text");

    lua_pushcfunction(L, l_lsFiles);
    lua_setglobal(L, "ls_files");

    lua_pushcfunction(L, l_showNotifications);
    lua_setglobal(L, "show_notifications");

    lua_pushcfunction(L, l_wrench_get_adb_devices);
    lua_setglobal(L, "adb_devices");

    lua_pushcfunction(L, l_adb_push);
    lua_setglobal(L, "qt_adb_push");

    lua_pushcfunction(L, l_adb_pull);
    lua_setglobal(L, "qt_adb_pull");

    lua_pushcfunction(L, l_adb_install);
    lua_setglobal(L, "qt_adb_install");

    lua_pushcfunction(L, l_qt_adb_pipe);
    lua_setglobal(L, "qt_adb_pipe");

    lua_pushcfunction(L, l_logToUI);
    lua_setglobal(L, "log_to_ui");

    lua_pushcfunction(L, l_adbQuickInputAm);
    lua_setglobal(L, "adb_quick_input");

    lua_pushcfunction(L, l_adbQuickInputAm);
    lua_setglobal(L, "adb_quick_am");

    lua_pushcfunction(L, l_is_exiting);
    lua_setglobal(L, "is_exiting");

    lua_pushcfunction(L, l_wrench_set_variable);
    lua_setglobal(L, "wrench_set_variable");

    lua_pushcfunction(L, l_wrench_get_variable);
    lua_setglobal(L, "wrench_get_variable");

    lua_pushcfunction(L, l_wrench_set_proc_var);
    lua_setglobal(L, "wrench_set_proc_var");

    lua_pushcfunction(L, l_wrench_get_proc_var);
    lua_setglobal(L, "wrench_get_proc_var");

    int error = luaL_loadstring(L, "wrench = require('wrench')") || lua_pcall(L, 0, 0, 0);
    if (error) {
        emit gotSomeLog("exit", QString().sprintf("Can't load wrench: %s", lua_tolstring(L, -1, NULL)));
        lua_close(L);
        return;
    }

    lua_getglobal(L, "wrench");
    while (true) {
        QStringList script;
        mMutex.lock();
        if (mActions.length() == 0) {
            mWait.wait(&mMutex);
        }
        if (mQuit) {
            mMutex.unlock();
            lua_close(L);
            return;
        }

        script = mActions.at(0);
        mActions.removeFirst();
        mMutex.unlock();
        QString func = script.at(0);
        lua_getfield(L, -1, qPrintable(func));
        script.pop_front();
        foreach (const QString& str, script) {
            lua_pushstring(L, str.toUtf8().constData());
        }
        error = lua_pcall(L, script.length(), 1, 0);
        if (error) {
            QString key = "exit";
            if (mQuit) {
                key = "quit";
            }
            emit gotSomeLog(key, QString().sprintf("Can't run %s: %s", qPrintable(func), lua_tolstring(L, -1, NULL)));
            lua_close(L);
            return;
        }
        if (lua_isstring(L, -1)) {
            emit gotSomeLog("info", lua_tolstring(L, -1, NULL));
        }
        lua_pop(L, 1);
    }
}

LuaExecuteThread::LuaExecuteThread(QObject* parent)
    : QThread(parent),
      mQuit(false),
      wrenchSock(NULL)
{
    ignoreOkScripts << "notification_arrived";
}

LuaExecuteThread::~LuaExecuteThread()
{
    if (wrenchSock)
        wrenchSock->deleteLater();
    qDebug() << "LuaExecuteThread::~LuaExecuteThread()";
}

void LuaExecuteThread::addScript(QStringList script)
{
    extern QString prompt_user(const QString &info, QMessageBox::StandardButtons buttons = QMessageBox::Ok);
    if (!this->isRunning()) {
        if (ignoreOkScripts.contains(script[0])) {
            // do nothing
        } else {
            emit gotSomeLog("Error", (QStringList() << "Can't run script:" << script).join(" "));
        }
        return;
    }

    if (this != that) {
        that = this;
    }
    mMutex.lock();
    mActions.append(script);
    mMutex.unlock();
    mWait.wakeOne();
}

void LuaExecuteThread::quitLua()
{
    mMutex.lock();
    mQuit = true;
    mMutex.unlock();
    mWait.wakeOne();
}

void LuaExecuteThread::clickNotification(const QStringList& args)
{
    foreach (const QString& s, args) {
        emit sigClickNotification(s);
    }
}

void LuaExecuteThread::gotUiTask(const QStringList& args)
{
    if (args.length() > 0) {
        QString cmd = args[0];
        QStringList realArgs = args.mid(1);
        emit gotUiTaskSig(cmd, realArgs);
    }
}

void LuaExecuteThread::selectApps()
{
    emit selectAppsSig();
}

void LuaExecuteThread::showNotifications()
{
    emit showNotificationsSig();
}

void LuaExecuteThread::on_argSelected(const QString& arg)
{
    mSelectArgsMutex.lock();
    mSelectedArg = arg;
    mSelectArgsMutex.unlock();
    mSelectArgsWait.wakeOne();
}

void LuaExecuteThread::logToUI(const char *log)
{
    emit gotSomeLog("log", log);
}
