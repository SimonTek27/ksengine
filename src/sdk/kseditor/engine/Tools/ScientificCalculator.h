#pragma once

#include <QWidget>
#include <QTabWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QStack>
#include <QVector>
#include <QComboBox>
#include <QTextBrowser>

namespace ks {

class ScientificCalculator : public QWidget
{
    Q_OBJECT

public:
    explicit ScientificCalculator(QWidget* parent = nullptr);
    ~ScientificCalculator() = default;

    static ScientificCalculator* launch(QWidget* parent = nullptr);

private:
    QTabWidget* m_tabWidget;

    // Standard tab
    QLineEdit* m_stdInput;
    QLineEdit* m_stdResult;
    QString m_stdExpr;
    QString m_stdExprCalc;

    // Scientific tab
    QLineEdit* m_sciInput;
    QLineEdit* m_sciResult;
    QString m_sciExpr;
    QString m_sciExprCalc;

    // Permutation/Combination tab
    QLineEdit* m_permA1;
    QLineEdit* m_permA2;
    QLineEdit* m_combC1;
    QLineEdit* m_combC2;
    QLineEdit* m_factInput;
    QTextBrowser* m_permResult;
    QTextBrowser* m_combResult;
    QTextBrowser* m_factResult;

    // Complex tab
    QLineEdit* m_cxReal1;
    QLineEdit* m_cxImag1;
    QLineEdit* m_cxReal2;
    QLineEdit* m_cxImag2;
    QLineEdit* m_cxResult;
    QComboBox* m_cxOp;

    bool m_stdPointFlag;
    bool m_sciPointFlag;

    // UI setup
    QWidget* createStandardTab();
    QWidget* createScientificTab();
    QWidget* createPermutationTab();
    QWidget* createComplexTab();

    // Standard tab slots
    void onStdButton();

    // Scientific tab slots
    void onSciButton();

    // Permutation/Combination slots
    void onPermDigit();
    void onPermDelete();
    void onCombCalc();
    void onPermCalc();
    void onFactCalc();

    // Complex slots
    void onCxDigit();
    void onCxClear();
    void onCxBack();
    void onCxEqual();

    // Shared calculation
    void infixToSuffix(const char* S, char OPS[], int& len);
    void calculateSuffix(char SUF[], int len, double& result, bool& flag);
    bool refuseCompositeFunction(const QString& inp);

    // Permutation/Combination math
    long long combinate(int n, int m);
    long long arrange(int n, int m);
    QVector<int> primProduce();
    int cal(int x, int p);
    int powMod(long long n, int k);

    // Helpers
    static QChar piChar();
    QString digitBtnStyle();
    QString opBtnStyle();
    QString funcBtnStyle();
};

} // namespace ks
