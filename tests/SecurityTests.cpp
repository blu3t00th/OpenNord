#include "windows/Security.h"

#include <QScopeGuard>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QtTest>
#include <sddl.h>
#include <aclapi.h>

class SecurityTests final : public QObject
{
    Q_OBJECT
private slots:
    void interactiveClientsCannotCreatePipeInstances();
    void privateFilesDoNotInheritUserReadAccess();
};

void SecurityTests::interactiveClientsCannotCreatePipeInstances()
{
    PSECURITY_DESCRIPTOR descriptor{};
    QString error;
    const auto attributes = opennord::windows::pipeSecurityAttributes(descriptor, error);
    const auto cleanup = qScopeGuard([&] { if (descriptor) LocalFree(descriptor); });
    QVERIFY2(descriptor, qPrintable(error));
    QVERIFY(!attributes.bInheritHandle);
    BOOL present{}, defaulted{};
    PACL acl{};
    QVERIFY(GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted));
    QVERIFY(present && acl && !defaulted);
    QCOMPARE(acl->AceCount, WORD(3));

    BYTE interactiveSid[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof(interactiveSid);
    QVERIFY(CreateWellKnownSid(WinInteractiveSid, nullptr, interactiveSid, &sidSize));
    bool foundInteractive{};
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *rawAce{};
        QVERIFY(GetAce(acl, index, &rawAce));
        const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(rawAce);
        QCOMPARE(ace->Header.AceType, BYTE(ACCESS_ALLOWED_ACE_TYPE));
        if (!EqualSid(const_cast<DWORD *>(&ace->SidStart), interactiveSid)) continue;
        foundInteractive = true;
        GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
        auto mask = ace->Mask;
        MapGenericMask(&mask, &mapping);
        QVERIFY(mask & FILE_READ_DATA);
        QVERIFY(mask & FILE_WRITE_DATA);
        QVERIFY(mask & SYNCHRONIZE);
        QVERIFY(!(mask & FILE_CREATE_PIPE_INSTANCE));
        QVERIFY(!(mask & WRITE_DAC));
        QVERIFY(!(mask & WRITE_OWNER));
    }
    QVERIFY(foundInteractive);
}

void SecurityTests::privateFilesDoNotInheritUserReadAccess()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("private-test"));
    QString error;
    QVERIFY2(opennord::windows::writePrivateFile(path, QByteArrayLiteral("test contents"), error), qPrintable(error));
    QCOMPARE(QFileInfo(path).size(), qint64(13));
    QVERIFY2(opennord::windows::writePrivateFile(path, QByteArrayLiteral("updated contents"), error), qPrintable(error));
    QCOMPARE(QFileInfo(path).size(), qint64(16));
    PSECURITY_DESCRIPTOR descriptor{};
    PACL acl{};
    auto mutablePath = path.toStdWString();
    QCOMPARE(GetNamedSecurityInfoW(mutablePath.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &acl, nullptr, &descriptor), DWORD(ERROR_SUCCESS));
    const auto cleanup = qScopeGuard([&] { LocalFree(descriptor); });
    QVERIFY(acl);
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision{};
    QVERIFY(GetSecurityDescriptorControl(descriptor, &control, &revision));
    QVERIFY(control & SE_DACL_PROTECTED);
    QCOMPARE(acl->AceCount, WORD(2));
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void *rawAce{};
        QVERIFY(GetAce(acl, index, &rawAce));
        const auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(rawAce);
        const auto sid = const_cast<DWORD *>(&ace->SidStart);
        QVERIFY(IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid));
        QVERIFY(!(ace->Header.AceFlags & INHERITED_ACE));
    }
    // A failed write must not silently fall back to a different destination.
    QVERIFY(!opennord::windows::writePrivateFile(directory.filePath(QStringLiteral("missing/file")), QByteArrayLiteral("secret"), error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(SecurityTests)
#include "SecurityTests.moc"
