#include <QtTest/QtTest>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QQueue>
#include <functional>
#include <algorithm>

// ============================================================================
// Minimal standalone copy of Version + VersionSpec + DependencyResolver logic
// (extracted from ModManager.h/cpp for independent testing)
// ============================================================================

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString preRelease;

    static Version fromString(const QString& str) {
        Version v;
        QString s = str.trimmed();
        if (s.startsWith('v', Qt::CaseInsensitive)) s = s.mid(1);
        int preIdx = -1;
        for (int i = 0; i < s.size(); ++i) {
            if (!s[i].isDigit() && s[i] != '.') { preIdx = i; break; }
        }
        QString verPart = s;
        if (preIdx >= 0) { verPart = s.left(preIdx); v.preRelease = s.mid(preIdx); }
        QStringList parts = verPart.split('.');
        v.major = parts.size() > 0 ? parts[0].toInt() : 0;
        v.minor = parts.size() > 1 ? parts[1].toInt() : 0;
        v.patch = parts.size() > 2 ? parts[2].toInt() : 0;
        return v;
    }

    int compare(const Version& other) const {
        if (major != other.major) return major - other.major;
        if (minor != other.minor) return minor - other.minor;
        if (patch != other.patch) return patch - other.patch;
        if (preRelease.isEmpty() != other.preRelease.isEmpty())
            return preRelease.isEmpty() ? 1 : -1;
        return preRelease.compare(other.preRelease);
    }

    bool operator==(const Version& other) const { return compare(other) == 0; }
    bool operator!=(const Version& other) const { return !(*this == other); }
    bool operator<(const Version& other) const { return compare(other) < 0; }
    bool operator<=(const Version& other) const { return compare(other) <= 0; }
    bool operator>(const Version& other) const { return compare(other) > 0; }
    bool operator>=(const Version& other) const { return compare(other) >= 0; }
    QString toString() const {
        QString s = QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
        if (!preRelease.isEmpty()) s += preRelease;
        return s;
    }
    bool isValid() const { return major >= 0 && minor >= 0 && patch >= 0; }
};

struct VersionSpec {
    enum Op { Any, Eq, Neq, Gt, Lt, Ge, Le, Tilde, Caret };
    Version version;
    Op op = Any;
    QString raw;

    bool matches(const Version& v) const {
        if (op == Any) return true;
        int cmp = version.compare(v);
        switch (op) {
        case Eq:    return cmp == 0;
        case Neq:   return cmp != 0;
        case Gt:    return cmp < 0;
        case Lt:    return cmp > 0;
        case Ge:    return cmp <= 0;
        case Le:    return cmp >= 0;
        case Tilde: return cmp <= 0 && v.major == version.major && v.minor == version.minor;
        case Caret:
            if (cmp > 0) return false;
            if (version.major != 0) return v.major == version.major;
            if (version.minor != 0) return v.minor == version.minor;
            return v.patch == version.patch;
        default: return false;
        }
    }
    bool isValid() const { return op == Any || version.isValid(); }

    static VersionSpec fromString(const QString& spec) {
        VersionSpec vs;
        vs.raw = spec.trimmed();
        if (vs.raw.isEmpty() || vs.raw == "*") { vs.op = Any; return vs; }
        struct { QString tok; Op op; } prefixes[] = {
            { ">=", Ge }, { "<=", Le }, { "!=", Neq },
            { ">", Gt },  { "<", Lt },  { "==", Eq },
            { "~>", Tilde }, { "^", Caret }
        };
        for (const auto& pfx : prefixes) {
            if (vs.raw.startsWith(pfx.tok)) {
                vs.op = pfx.op;
                vs.version = Version::fromString(vs.raw.mid(pfx.tok.size()));
                return vs;
            }
        }
        vs.op = Eq;
        vs.version = Version::fromString(vs.raw);
        return vs;
    }

    static QString opToString(Op o) {
        switch (o) {
        case Any:   return "*";
        case Eq:    return "==";
        case Neq:   return "!=";
        case Gt:    return ">";
        case Lt:    return "<";
        case Ge:    return ">=";
        case Le:    return "<=";
        case Tilde: return "~>";
        case Caret: return "^";
        default:    return "?";
        }
    }
};

struct ModEntry {
    QString name;
    Version version;
    QString versionStr;
    bool enabled = true;
    QStringList dependencies;
    QMap<QString, VersionSpec> dependencySpecs;
    QStringList conflicts;
};

class DependencyResolver {
public:
    struct ResolvedDep {
        QString depName;
        VersionSpec spec;
        bool satisfied = false;
        QString installedVersion;
        QString resolvedBy;
    };

    struct Resolution {
        bool satisfied = true;
        QStringList missingDeps;
        QStringList conflictingMods;
        QStringList resolvedOrder;
        QStringList circularDeps;
        QMap<QString, QVector<ResolvedDep>> depDetails;
    };

    Resolution resolve(const QVector<ModEntry>& allMods, const QStringList& targetMods) {
        Resolution result;
        result.missingDeps = findMissing(allMods, targetMods);
        result.conflictingMods = findConflicts(allMods, targetMods);
        result.satisfied = result.missingDeps.isEmpty() && result.conflictingMods.isEmpty();

        QSet<QString> visited;
        QSet<QString> inStack;
        std::function<bool(const QString&)> detectCycle = [&](const QString& modName) -> bool {
            if (inStack.contains(modName)) return true;
            if (visited.contains(modName)) return false;
            visited.insert(modName);
            inStack.insert(modName);
            auto it = std::find_if(allMods.begin(), allMods.end(),
                [&](const ModEntry& m) { return m.name == modName; });
            if (it != allMods.end()) {
                for (const QString& dep : it->dependencies) {
                    if (detectCycle(dep)) return true;
                }
            }
            inStack.remove(modName);
            return false;
        };

        for (const QString& t : targetMods) {
            if (detectCycle(t)) {
                result.circularDeps.append(t);
                result.satisfied = false;
            }
        }

        if (result.satisfied) {
            result.resolvedOrder = topologicalSort(allMods);
        }

        for (const QString& t : targetMods) {
            result.depDetails[t] = resolveDependencyDetails(allMods, t);
        }

        QStringList versionConflicts = findVersionConflicts(allMods, targetMods);
        if (!versionConflicts.isEmpty()) {
            result.conflictingMods.append(versionConflicts);
            result.satisfied = false;
        }

        return result;
    }

    QStringList topologicalSort(const QVector<ModEntry>& mods) {
        QMap<QString, int> inDegree;
        QMap<QString, QStringList> adj;
        for (const auto& mod : mods) {
            if (!inDegree.contains(mod.name)) inDegree[mod.name] = 0;
            for (const QString& dep : mod.dependencies) {
                adj[dep].append(mod.name);
                inDegree[mod.name]++;
            }
        }
        QQueue<QString> queue;
        for (auto it = inDegree.begin(); it != inDegree.end(); ++it) {
            if (it.value() == 0) queue.enqueue(it.key());
        }
        QStringList sorted;
        while (!queue.isEmpty()) {
            QString node = queue.dequeue();
            sorted.append(node);
            for (const QString& neighbor : adj[node]) {
                inDegree[neighbor]--;
                if (inDegree[neighbor] == 0) queue.enqueue(neighbor);
            }
        }
        return sorted;
    }

    QStringList findMissing(const QVector<ModEntry>& allMods, const QStringList& targetMods) {
        QSet<QString> available;
        for (const auto& mod : allMods) available.insert(mod.name);

        QStringList missing;
        QSet<QString> checked;
        std::function<void(const QString&)> checkDeps = [&](const QString& modName) {
            if (checked.contains(modName)) return;
            checked.insert(modName);
            auto it = std::find_if(allMods.begin(), allMods.end(),
                [&](const ModEntry& m) { return m.name == modName; });
            if (it == allMods.end()) {
                if (!available.contains(modName)) missing.append(modName);
                return;
            }
            for (const QString& dep : it->dependencies) {
                if (!available.contains(dep)) missing.append(dep);
                checkDeps(dep);
            }
        };
        for (const QString& t : targetMods) checkDeps(t);
        missing.removeDuplicates();
        return missing;
    }

    QStringList findConflicts(const QVector<ModEntry>& allMods, const QStringList& targetMods) {
        QStringList conflicts;
        QSet<QString> targetSet(targetMods.begin(), targetMods.end());
        for (const auto& mod : allMods) {
            if (!targetSet.contains(mod.name) && mod.enabled) {
                for (const QString& c : mod.conflicts) {
                    if (targetSet.contains(c))
                        conflicts.append(c + " conflicts with " + mod.name);
                }
            }
        }
        return conflicts;
    }

    bool checkConstraint(const VersionSpec& spec, const Version& installed) const {
        return spec.matches(installed);
    }

    QVector<ResolvedDep> resolveDependencyDetails(const QVector<ModEntry>& allMods,
                                                   const QString& modName) const {
        QVector<ResolvedDep> result;
        auto it = std::find_if(allMods.begin(), allMods.end(),
            [&](const ModEntry& m) { return m.name == modName; });
        if (it == allMods.end()) return result;

        QSet<QString> depSet;
        std::function<void(const ModEntry&, int)> collect = [&](const ModEntry& mod, int depth) {
            if (depth > 20) return;
            for (const QString& dep : mod.dependencies) {
                if (depSet.contains(dep)) continue;
                depSet.insert(dep);
                ResolvedDep rd;
                rd.depName = dep;
                rd.spec = mod.dependencySpecs.value(dep);
                auto prov = std::find_if(allMods.begin(), allMods.end(),
                    [&](const ModEntry& m) { return m.name == dep && m.enabled; });
                if (prov != allMods.end()) {
                    rd.installedVersion = prov->version.toString();
                    rd.resolvedBy = prov->name;
                    rd.satisfied = rd.spec.isValid() ? checkConstraint(rd.spec, prov->version) : true;
                } else {
                    rd.satisfied = false;
                }
                result.append(rd);
                if (prov != allMods.end()) collect(*prov, depth + 1);
            }
        };
        collect(*it, 0);
        return result;
    }

    QStringList reverseDependencies(const QVector<ModEntry>& allMods,
                                     const QString& modName) const {
        QStringList reverse;
        for (const auto& mod : allMods) {
            if (!mod.enabled) continue;
            for (const QString& dep : mod.dependencies) {
                if (dep == modName) { reverse.append(mod.name); break; }
            }
        }
        return reverse;
    }

    QStringList findVersionConflicts(const QVector<ModEntry>& allMods,
                                      const QStringList& targetMods) const {
        QMap<QString, QVector<QPair<QString, VersionSpec>>> depUsers;
        for (const QString& t : targetMods) {
            auto it = std::find_if(allMods.begin(), allMods.end(),
                [&](const ModEntry& m) { return m.name == t; });
            if (it == allMods.end()) continue;
            for (auto ds = it->dependencySpecs.begin(); ds != it->dependencySpecs.end(); ++ds) {
                depUsers[ds.key()].append({t, ds.value()});
            }
        }
        QStringList conflicts;
        for (auto it = depUsers.begin(); it != depUsers.end(); ++it) {
            const QString& depName = it.key();
            auto& users = it.value();
            if (users.size() < 2) continue;
            auto instIt = std::find_if(allMods.begin(), allMods.end(),
                [&](const ModEntry& m) { return m.name == depName && m.enabled; });
            if (instIt == allMods.end()) continue;
            for (const auto& user : users) {
                if (!checkConstraint(user.second, instIt->version)) {
                    conflicts.append(QString("%1 requires %2 %3 %4, installed is %5")
                        .arg(user.first, depName,
                             VersionSpec::opToString(user.second.op),
                             user.second.version.toString(),
                             instIt->version.toString()));
                }
            }
        }
        return conflicts;
    }
};

// ============================================================================
// Tests
// ============================================================================

class TestModManagerDeps : public QObject {
    Q_OBJECT

private slots:
    void testVersionParsing();
    void testVersionComparison();
    void testVersionSpecAny();
    void testVersionSpecExact();
    void testVersionSpecGreaterThan();
    void testVersionSpecLessThan();
    void testVersionSpecRange();
    void testVersionSpecCaret();
    void testVersionSpecTilde();
    void testConstraintMatch();
    void testConstraintMismatch();
    void testFindMissing();
    void testDependencyDetails();
    void testReverseDependencies();
    void testCircularDependency();
    void testVersionConflictDetection();
    void testTopologicalSort();
};

void TestModManagerDeps::testVersionParsing()
{
    Version v1 = Version::fromString("1.2.3");
    QCOMPARE(v1.major, 1);
    QCOMPARE(v1.minor, 2);
    QCOMPARE(v1.patch, 3);
    QVERIFY(v1.preRelease.isEmpty());

    Version v2 = Version::fromString("v2.0.0");
    QCOMPARE(v2.major, 2);
    QCOMPARE(v2.minor, 0);
    QCOMPARE(v2.patch, 0);

    Version v3 = Version::fromString("v1.0.0-alpha");
    QCOMPARE(v3.major, 1);
    QCOMPARE(v3.preRelease, QString("-alpha"));

    Version v4 = Version::fromString("0.5");
    QCOMPARE(v4.major, 0);
    QCOMPARE(v4.minor, 5);
    QCOMPARE(v4.patch, 0);

    Version v5 = Version::fromString("*");
    QCOMPARE(v5.major, 0);
    QCOMPARE(v5.minor, 0);
    QCOMPARE(v5.patch, 0);
}

void TestModManagerDeps::testVersionComparison()
{
    QVERIFY(Version::fromString("1.0.0") > Version::fromString("0.9.9"));
    QVERIFY(Version::fromString("1.0.0") < Version::fromString("1.0.1"));
    QVERIFY(Version::fromString("2.0.0") == Version::fromString("2.0.0"));
    QVERIFY(Version::fromString("1.0.0") > Version::fromString("1.0.0-rc1")); // release > pre-release
    QVERIFY(Version::fromString("1.0.0") >= Version::fromString("1.0.0"));
    QVERIFY(Version::fromString("0.8.0") <= Version::fromString("1.0.0"));
}

void TestModManagerDeps::testVersionSpecAny()
{
    VersionSpec spec = VersionSpec::fromString("");
    QCOMPARE(spec.op, VersionSpec::Any);
    QVERIFY(spec.matches(Version::fromString("1.0.0")));
    QVERIFY(spec.matches(Version::fromString("999.0.0")));

    spec = VersionSpec::fromString("*");
    QCOMPARE(spec.op, VersionSpec::Any);
}

void TestModManagerDeps::testVersionSpecExact()
{
    VersionSpec spec = VersionSpec::fromString("1.2.3");
    QCOMPARE(spec.op, VersionSpec::Eq);
    QCOMPARE(spec.version.major, 1);
    QCOMPARE(spec.version.minor, 2);
    QCOMPARE(spec.version.patch, 3);
    QVERIFY(spec.matches(Version::fromString("1.2.3")));
    QVERIFY(!spec.matches(Version::fromString("1.2.4")));

    spec = VersionSpec::fromString("==2.0.0");
    QCOMPARE(spec.op, VersionSpec::Eq);
    QVERIFY(spec.matches(Version::fromString("2.0.0")));
    QVERIFY(!spec.matches(Version::fromString("2.0.1")));
}

void TestModManagerDeps::testVersionSpecGreaterThan()
{
    VersionSpec spec = VersionSpec::fromString(">1.0.0");
    QCOMPARE(spec.op, VersionSpec::Gt);
    QVERIFY(spec.matches(Version::fromString("1.0.1")));
    QVERIFY(!spec.matches(Version::fromString("1.0.0")));
    QVERIFY(!spec.matches(Version::fromString("0.9.9")));

    spec = VersionSpec::fromString(">=2.0.0");
    QCOMPARE(spec.op, VersionSpec::Ge);
    QVERIFY(spec.matches(Version::fromString("2.0.0")));
    QVERIFY(spec.matches(Version::fromString("2.0.1")));
    QVERIFY(!spec.matches(Version::fromString("1.9.9")));
}

void TestModManagerDeps::testVersionSpecLessThan()
{
    VersionSpec spec = VersionSpec::fromString("<1.0.0");
    QCOMPARE(spec.op, VersionSpec::Lt);
    QVERIFY(spec.matches(Version::fromString("0.9.9")));
    QVERIFY(!spec.matches(Version::fromString("1.0.0")));

    spec = VersionSpec::fromString("<=1.0.0");
    QCOMPARE(spec.op, VersionSpec::Le);
    QVERIFY(spec.matches(Version::fromString("1.0.0")));
    QVERIFY(spec.matches(Version::fromString("0.9.9")));
    QVERIFY(!spec.matches(Version::fromString("1.0.1")));
}

void TestModManagerDeps::testVersionSpecRange()
{
    VersionSpec spec = VersionSpec::fromString("!=1.0.0");
    QCOMPARE(spec.op, VersionSpec::Neq);
    QVERIFY(!spec.matches(Version::fromString("1.0.0")));
    QVERIFY(spec.matches(Version::fromString("1.0.1")));
    QVERIFY(spec.matches(Version::fromString("0.9.9")));
}

void TestModManagerDeps::testVersionSpecCaret()
{
    VersionSpec spec = VersionSpec::fromString("^1.2.3");
    QCOMPARE(spec.op, VersionSpec::Caret);
    QVERIFY(spec.matches(Version::fromString("1.2.3")));
    QVERIFY(spec.matches(Version::fromString("1.9.9")));
    QVERIFY(!spec.matches(Version::fromString("2.0.0")));
    QVERIFY(!spec.matches(Version::fromString("1.2.2")));

    spec = VersionSpec::fromString("^0.2.3");
    QVERIFY(spec.matches(Version::fromString("0.2.3")));
    QVERIFY(spec.matches(Version::fromString("0.2.9")));
    QVERIFY(!spec.matches(Version::fromString("0.3.0")));
}

void TestModManagerDeps::testVersionSpecTilde()
{
    VersionSpec spec = VersionSpec::fromString("~>1.2.3");
    QCOMPARE(spec.op, VersionSpec::Tilde);
    QVERIFY(spec.matches(Version::fromString("1.2.3")));
    QVERIFY(spec.matches(Version::fromString("1.2.9")));
    QVERIFY(!spec.matches(Version::fromString("1.3.0")));
    QVERIFY(!spec.matches(Version::fromString("1.2.2")));
}

void TestModManagerDeps::testConstraintMatch()
{
    DependencyResolver resolver;
    QVERIFY(resolver.checkConstraint(VersionSpec::fromString(">=1.0.0"),
                                     Version::fromString("2.0.0")));
    QVERIFY(resolver.checkConstraint(VersionSpec::fromString("<2.0.0"),
                                     Version::fromString("1.0.0")));
    QVERIFY(resolver.checkConstraint(VersionSpec::fromString("1.0.0"),
                                     Version::fromString("1.0.0")));
}

void TestModManagerDeps::testConstraintMismatch()
{
    DependencyResolver resolver;
    QVERIFY(!resolver.checkConstraint(VersionSpec::fromString(">=2.0.0"),
                                       Version::fromString("1.0.0")));
    QVERIFY(!resolver.checkConstraint(VersionSpec::fromString("==1.0.0"),
                                       Version::fromString("1.0.1")));
    QVERIFY(!resolver.checkConstraint(VersionSpec::fromString("^2.0.0"),
                                       Version::fromString("3.0.0")));
}

void TestModManagerDeps::testFindMissing()
{
    DependencyResolver resolver;
    QVector<ModEntry> mods;
    ModEntry a; a.name = "ModA"; a.version = Version::fromString("1.0.0"); a.enabled = true; mods.append(a);
    ModEntry b; b.name = "ModB"; b.version = Version::fromString("1.0.0"); b.enabled = true;
    b.dependencies.append("ModA"); mods.append(b);

    QStringList missing = resolver.findMissing(mods, {"ModB"});
    QVERIFY(missing.isEmpty());

    ModEntry c; c.name = "ModC"; c.version = Version::fromString("1.0.0"); c.enabled = true;
    c.dependencies.append("ModX"); mods.append(c);

    missing = resolver.findMissing(mods, {"ModC"});
    QCOMPARE(missing.size(), 1);
    QCOMPARE(missing[0], QString("ModX"));
}

void TestModManagerDeps::testDependencyDetails()
{
    DependencyResolver resolver;
    QVector<ModEntry> mods;

    ModEntry dep; dep.name = "LibA"; dep.version = Version::fromString("2.0.0"); dep.enabled = true; mods.append(dep);
    ModEntry app; app.name = "App"; app.version = Version::fromString("1.0.0"); app.enabled = true;
    app.dependencies.append("LibA");
    app.dependencySpecs["LibA"] = VersionSpec::fromString(">=1.0.0");
    mods.append(app);

    auto details = resolver.resolveDependencyDetails(mods, "App");
    QCOMPARE(details.size(), 1);
    QVERIFY(details[0].satisfied);
    QCOMPARE(details[0].installedVersion, QString("2.0.0"));
}

void TestModManagerDeps::testReverseDependencies()
{
    DependencyResolver resolver;
    QVector<ModEntry> mods;
    ModEntry lib; lib.name = "LibA"; lib.enabled = true; mods.append(lib);
    ModEntry app; app.name = "App"; app.enabled = true;
    app.dependencies.append("LibA"); mods.append(app);
    ModEntry other; other.name = "Other"; other.enabled = true;
    other.dependencies.append("LibA"); mods.append(other);

    auto reverse = resolver.reverseDependencies(mods, "LibA");
    QCOMPARE(reverse.size(), 2);
    QVERIFY(reverse.contains("App"));
    QVERIFY(reverse.contains("Other"));
}

void TestModManagerDeps::testCircularDependency()
{
    DependencyResolver resolver;
    QVector<ModEntry> mods;
    ModEntry a; a.name = "A"; a.enabled = true;
    a.dependencies.append("B"); mods.append(a);
    ModEntry b; b.name = "B"; b.enabled = true;
    b.dependencies.append("C"); mods.append(b);
    ModEntry c; c.name = "C"; c.enabled = true;
    c.dependencies.append("A"); mods.append(c);

    auto res = resolver.resolve(mods, {"A"});
    QVERIFY(!res.satisfied);
    QVERIFY(!res.circularDeps.isEmpty());
}

void TestModManagerDeps::testVersionConflictDetection()
{
    DependencyResolver resolver;
    QVector<ModEntry> mods;

    ModEntry lib; lib.name = "LibA"; lib.version = Version::fromString("1.0.0"); lib.enabled = true; mods.append(lib);

    ModEntry app1; app1.name = "App1"; app1.enabled = true;
    app1.dependencies.append("LibA");
    app1.dependencySpecs["LibA"] = VersionSpec::fromString(">=1.0.0");
    mods.append(app1);

    ModEntry app2; app2.name = "App2"; app2.enabled = true;
    app2.dependencies.append("LibA");
    app2.dependencySpecs["LibA"] = VersionSpec::fromString(">=2.0.0"); // conflict!
    mods.append(app2);

    auto conflicts = resolver.findVersionConflicts(mods, {"App1", "App2"});
    QVERIFY(!conflicts.isEmpty());
    QVERIFY(conflicts[0].contains("App2"));
    QVERIFY(conflicts[0].contains("2.0.0"));
}

void TestModManagerDeps::testTopologicalSort()
{
    DependencyResolver resolver;
    QVector<ModEntry> mods;

    ModEntry a; a.name = "A"; a.enabled = true;
    a.dependencies.append("B"); mods.append(a);
    ModEntry b; b.name = "B"; b.enabled = true;
    b.dependencies.append("C"); mods.append(b);
    ModEntry c; c.name = "C"; c.enabled = true; mods.append(c);

    auto sorted = resolver.topologicalSort(mods);
    QCOMPARE(sorted.size(), 3);

    // C must come before B, which comes before A
    int idxC = sorted.indexOf("C");
    int idxB = sorted.indexOf("B");
    int idxA = sorted.indexOf("A");
    QVERIFY(idxC < idxB);
    QVERIFY(idxB < idxA);
}

QTEST_MAIN(TestModManagerDeps)
#include "test_ModManagerDeps.moc"
