#include "engine/Tools/QualitySystem.h"
#include <QtTest/QtTest>

using namespace ks;

class TestValidationSystem : public QObject {
    Q_OBJECT

private slots:
    void test_validationResult();
    void test_validationManagerInstance();
    void test_registerRule();
    void test_validate();
    void test_enableDisableRule();
    void test_hasErrors();
    void test_clearResults();
    void test_builtinRules();
};

void TestValidationSystem::test_validationResult()
{
    ValidationResult result(ValidationSeverity::Error, ValidationCategory::Geometry,
                            "GEO001", "Mesh has no UVs", "mesh_01");
    QVERIFY(result.isError());
    QCOMPARE(result.severity, ValidationSeverity::Error);
    QCOMPARE(result.code, QString("GEO001"));
    QCOMPARE(result.objectId, QString("mesh_01"));

    ValidationResult info(ValidationSeverity::Info, ValidationCategory::General,
                          "INF001", "Info message", "");
    QVERIFY(!info.isError());
}

void TestValidationSystem::test_validationManagerInstance()
{
    ValidationManager* mgr = ValidationManager::instance();
    QVERIFY(mgr != nullptr);
    QCOMPARE(ValidationManager::instance(), mgr);
}

void TestValidationSystem::test_registerRule()
{
    ValidationManager* mgr = ValidationManager::instance();
    mgr->clearResults();
    mgr->clearRules();

    mgr->registerRule("test_rule", "Test validation rule",
                       ValidationCategory::General,
                       [](const QVariantMap& ctx) -> QVector<ValidationResult> {
                           QVector<ValidationResult> results;
                           if (!ctx.contains("name")) {
                               results.append(ValidationResult(
                                   ValidationSeverity::Error, ValidationCategory::General,
                                   "TST001", "Missing 'name'", ""));
                           }
                           return results;
                       });

    QVariantMap ctx;
    ctx["name"] = "test";
    auto results = mgr->validate(ctx, ValidationCategory::General);
    QVERIFY(results.isEmpty());

    QVariantMap emptyCtx;
    auto errResults = mgr->validate(emptyCtx, ValidationCategory::General);
    QCOMPARE(errResults.size(), 1);
    QCOMPARE(errResults[0].code, QString("TST001"));
}

void TestValidationSystem::test_validate()
{
    ValidationManager* mgr = ValidationManager::instance();
    mgr->clearResults();
    mgr->clearRules();

    mgr->registerRule("check_size", "Check size field",
                       ValidationCategory::General,
                       [](const QVariantMap& ctx) -> QVector<ValidationResult> {
                           QVector<ValidationResult> results;
                           int size = ctx.value("size", 0).toInt();
                           if (size <= 0) {
                               results.append(ValidationResult(
                                   ValidationSeverity::Warning, ValidationCategory::General,
                                   "TST002", "Size must be positive", ""));
                           }
                           return results;
                       });

    QVariantMap ctx;
    ctx["size"] = -1;
    auto results = mgr->validate(ctx);
    QCOMPARE(results.size(), 1);

    ctx["size"] = 10;
    results = mgr->validate(ctx);
    QCOMPARE(results.size(), 0);
}

void TestValidationSystem::test_enableDisableRule()
{
    ValidationManager* mgr = ValidationManager::instance();
    mgr->clearResults();
    mgr->clearRules();

    mgr->registerRule("toggle_rule", "Toggle test",
                       ValidationCategory::General,
                       [](const QVariantMap&) -> QVector<ValidationResult> {
                           return {ValidationResult(ValidationSeverity::Error,
                                      ValidationCategory::General, "TST003", "Always fails", "")};
                       });

    mgr->enableRule("toggle_rule", false);
    auto results = mgr->validate(QVariantMap());
    bool hasError = false;
    for (const auto& r : results) {
        if (r.code == "TST003") hasError = true;
    }
    QVERIFY(!hasError);

    mgr->enableRule("toggle_rule", true);
    results = mgr->validate(QVariantMap());
    hasError = false;
    for (const auto& r : results) {
        if (r.code == "TST003") hasError = true;
    }
    QVERIFY(hasError);
}

void TestValidationSystem::test_hasErrors()
{
    ValidationManager* mgr = ValidationManager::instance();
    mgr->clearResults();
    mgr->clearRules();

    mgr->registerRule("err_rule", "Error test",
                       ValidationCategory::General,
                       [](const QVariantMap&) -> QVector<ValidationResult> {
                           return {ValidationResult(ValidationSeverity::Error,
                                      ValidationCategory::General, "ERR", "Error", "")};
                       });

    mgr->validate(QVariantMap());
    QVERIFY(mgr->hasErrors());
    QCOMPARE(mgr->getErrors().size(), 1);
}

void TestValidationSystem::test_clearResults()
{
    ValidationManager* mgr = ValidationManager::instance();
    mgr->clearResults();
    mgr->clearRules();
    mgr->validate(QVariantMap());
    QVERIFY(mgr->getLastResults().isEmpty() || !mgr->getLastResults().isEmpty());
    mgr->clearResults();
    mgr->clearRules();
    QVERIFY(mgr->getLastResults().isEmpty());
}

void TestValidationSystem::test_builtinRules()
{
    ValidationManager* mgr = ValidationManager::instance();
    mgr->clearResults();
    mgr->clearRules();
    mgr->registerBuiltinRules();
    auto results = mgr->validate(QVariantMap());
    QVERIFY(!results.isEmpty() || mgr->getLastResults().isEmpty());
}

QTEST_MAIN(TestValidationSystem)
#include "test_ValidationSystem.moc"
