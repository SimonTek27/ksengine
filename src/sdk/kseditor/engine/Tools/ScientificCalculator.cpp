#include "ScientificCalculator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMessageBox>
#include <QFont>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ks {

static const int MOD = 100007;
static const int MAXN = 1000001;
static bool sieveStorage[MAXN] = {false};

QChar ScientificCalculator::piChar() { return QChar(0x03C0); }

QString ScientificCalculator::digitBtnStyle()
{
    return QStringLiteral(
        "QPushButton{font-size:14px;min-width:50px;min-height:50px;"
        "border:1px solid #888;border-radius:4px;background:#f0f0f0;}"
        "QPushButton:pressed{background:#d0d0d0;}");
}

QString ScientificCalculator::opBtnStyle()
{
    return QStringLiteral(
        "QPushButton{font-size:14px;min-width:50px;min-height:50px;"
        "border:1px solid #888;border-radius:4px;background:#e8e8e8;}"
        "QPushButton:pressed{background:#c8c8c8;}");
}

QString ScientificCalculator::funcBtnStyle()
{
    return QStringLiteral(
        "QPushButton{font-size:12px;min-width:50px;min-height:50px;"
        "border:1px solid #888;border-radius:4px;background:#ddeeff;}"
        "QPushButton:pressed{background:#bbccdd;}");
}

// ============================================================================
// Constructor
// ============================================================================

ScientificCalculator::ScientificCalculator(QWidget* parent)
    : QWidget(parent)
    , m_stdPointFlag(true)
    , m_sciPointFlag(true)
{
    setWindowTitle(QStringLiteral("Scientific Calculator"));
    setFixedSize(500, 550);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(5, 5, 5, 5);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabPosition(QTabWidget::North);
    m_tabWidget->setMovable(false);

    m_tabWidget->addTab(createStandardTab(), QStringLiteral("Standard"));
    m_tabWidget->addTab(createScientificTab(), QStringLiteral("Scientific"));
    m_tabWidget->addTab(createPermutationTab(), QStringLiteral("Perm/Comb"));
    m_tabWidget->addTab(createComplexTab(), QStringLiteral("Complex"));

    mainLayout->addWidget(m_tabWidget);
}

ScientificCalculator* ScientificCalculator::launch(QWidget* parent)
{
    auto* calc = new ScientificCalculator(parent);
    calc->setAttribute(Qt::WA_DeleteOnClose);
    calc->setWindowFlags(Qt::Window | Qt::Dialog);
    calc->setWindowTitle(QStringLiteral("Calculator"));
    calc->show();
    calc->raise();
    calc->activateWindow();
    return calc;
}

// ============================================================================
// Standard Tab
// ============================================================================

QWidget* ScientificCalculator::createStandardTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(new QLabel(QStringLiteral("Input")));
    m_stdInput = new QLineEdit();
    m_stdInput->setReadOnly(true);
    m_stdInput->setFont(QFont(QStringLiteral("Arial"), 12));
    inputRow->addWidget(m_stdInput);
    layout->addLayout(inputRow);

    auto* resultRow = new QHBoxLayout();
    resultRow->addWidget(new QLabel(QStringLiteral("Result")));
    m_stdResult = new QLineEdit();
    m_stdResult->setReadOnly(true);
    m_stdResult->setFont(QFont(QStringLiteral("Arial"), 12));
    resultRow->addWidget(m_stdResult);
    layout->addLayout(resultRow);

    auto* grid = new QGridLayout();
    grid->setSpacing(4);

    auto addStdBtn4 = [&](const QString& text, int row, int col) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(digitBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onStdButton);
        grid->addWidget(btn, row, col, 1, 1);
    };
    auto addStdBtnSpan = [&](const QString& text, int row, int col, int rs, int cs) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(digitBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onStdButton);
        grid->addWidget(btn, row, col, rs, cs);
    };

    auto addStdOp4 = [&](const QString& text, int row, int col) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(opBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onStdButton);
        grid->addWidget(btn, row, col, 1, 1);
    };
    auto addStdOpSpan = [&](const QString& text, int row, int col, int rs, int cs) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(opBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onStdButton);
        grid->addWidget(btn, row, col, rs, cs);
    };

    // Row 0: <-  C  (  )
    addStdOp4(QStringLiteral("<-"), 0, 0);
    addStdOpSpan(QStringLiteral("C"), 0, 1, 1, 2);
    addStdOp4(QStringLiteral("("), 0, 3);
    addStdOp4(QStringLiteral(")"), 0, 4);
    // Row 1: 7 8 9 + ^
    addStdBtn4(QStringLiteral("7"), 1, 0);
    addStdBtn4(QStringLiteral("8"), 1, 1);
    addStdBtn4(QStringLiteral("9"), 1, 2);
    addStdOp4(QStringLiteral("+"), 1, 3);
    addStdOp4(QStringLiteral("^"), 1, 4);
    // Row 2: 4 5 6 - sqrt
    addStdBtn4(QStringLiteral("4"), 2, 0);
    addStdBtn4(QStringLiteral("5"), 2, 1);
    addStdBtn4(QStringLiteral("6"), 2, 2);
    addStdOp4(QStringLiteral("-"), 2, 3);
    addStdOp4(QString::fromUtf8("\xe2\x88\x9a"), 2, 4); // sqrt
    // Row 3: 1 2 3 * =
    addStdBtn4(QStringLiteral("1"), 3, 0);
    addStdBtn4(QStringLiteral("2"), 3, 1);
    addStdBtn4(QStringLiteral("3"), 3, 2);
    addStdOp4(QStringLiteral("*"), 3, 3);
    addStdOpSpan(QStringLiteral("="), 3, 4, 2, 1);
    // Row 4: 0 . /
    addStdBtnSpan(QStringLiteral("0"), 4, 0, 1, 2);
    addStdBtn4(QStringLiteral("."), 4, 2);
    addStdOp4(QStringLiteral("/"), 4, 3);

    layout->addLayout(grid);
    return tab;
}

// ============================================================================
// Scientific Tab
// ============================================================================

QWidget* ScientificCalculator::createScientificTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(new QLabel(QStringLiteral("Input")));
    m_sciInput = new QLineEdit();
    m_sciInput->setReadOnly(true);
    m_sciInput->setFont(QFont(QStringLiteral("Arial"), 12));
    inputRow->addWidget(m_sciInput);
    layout->addLayout(inputRow);

    auto* resultRow = new QHBoxLayout();
    resultRow->addWidget(new QLabel(QStringLiteral("Result")));
    m_sciResult = new QLineEdit();
    m_sciResult->setReadOnly(true);
    m_sciResult->setFont(QFont(QStringLiteral("Arial"), 12));
    resultRow->addWidget(m_sciResult);
    layout->addLayout(resultRow);

    auto* grid = new QGridLayout();
    grid->setSpacing(3);

    auto addSciBtn4 = [&](const QString& text, int row, int col) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(digitBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onSciButton);
        grid->addWidget(btn, row, col, 1, 1);
    };
    auto addSciBtnSpan = [&](const QString& text, int row, int col, int rs, int cs) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(digitBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onSciButton);
        grid->addWidget(btn, row, col, rs, cs);
    };

    auto addSciOp4 = [&](const QString& text, int row, int col) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(opBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onSciButton);
        grid->addWidget(btn, row, col, 1, 1);
    };
    auto addSciOpSpan = [&](const QString& text, int row, int col, int rs, int cs) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(opBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onSciButton);
        grid->addWidget(btn, row, col, rs, cs);
    };

    auto addSciFunc4 = [&](const QString& text, int row, int col) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(funcBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onSciButton);
        grid->addWidget(btn, row, col, 1, 1);
    };

    // Row 0: mod  ^   pi  e   <-  C
    addSciFunc4(QStringLiteral("mod"), 0, 0);
    addSciOp4(QStringLiteral("^"), 0, 1);
    addSciFunc4(QStringLiteral("pi"), 0, 2);
    addSciFunc4(QStringLiteral("e"), 0, 3);
    addSciOp4(QStringLiteral("<-"), 0, 4);
    addSciOp4(QStringLiteral("C"), 0, 5);
    // Row 1: 1/x sqrt 7 8 9 + (
    addSciFunc4(QStringLiteral("1/x"), 1, 0);
    addSciFunc4(QString::fromUtf8("\xe2\x88\x9a"), 1, 1); // sqrt
    addSciBtn4(QStringLiteral("7"), 1, 2);
    addSciBtn4(QStringLiteral("8"), 1, 3);
    addSciBtn4(QStringLiteral("9"), 1, 4);
    addSciOp4(QStringLiteral("+"), 1, 5);
    addSciOp4(QStringLiteral("("), 1, 6);
    // Row 2: |x| exp 4 5 6 - )
    addSciFunc4(QStringLiteral("|x|"), 2, 0);
    addSciFunc4(QStringLiteral("exp"), 2, 1);
    addSciBtn4(QStringLiteral("4"), 2, 2);
    addSciBtn4(QStringLiteral("5"), 2, 3);
    addSciBtn4(QStringLiteral("6"), 2, 4);
    addSciOp4(QStringLiteral("-"), 2, 5);
    addSciOp4(QStringLiteral(")"), 2, 6);
    // Row 3: sin log 1 2 3 * =
    addSciFunc4(QStringLiteral("sin"), 3, 0);
    addSciFunc4(QStringLiteral("log"), 3, 1);
    addSciBtn4(QStringLiteral("1"), 3, 2);
    addSciBtn4(QStringLiteral("2"), 3, 3);
    addSciBtn4(QStringLiteral("3"), 3, 4);
    addSciOp4(QStringLiteral("*"), 3, 5);
    addSciOpSpan(QStringLiteral("="), 3, 6, 2, 1);
    // Row 4: cos ln 0 . /
    addSciFunc4(QStringLiteral("cos"), 4, 0);
    addSciFunc4(QStringLiteral("ln"), 4, 1);
    addSciBtnSpan(QStringLiteral("0"), 4, 2, 1, 2);
    addSciBtn4(QStringLiteral("."), 4, 4);
    addSciOp4(QStringLiteral("/"), 4, 5);

    layout->addLayout(grid);
    return tab;
}

// ============================================================================
// Permutation/Combination Tab
// ============================================================================

QWidget* ScientificCalculator::createPermutationTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    // Factorial
    auto* factRow = new QHBoxLayout();
    factRow->addWidget(new QLabel(QStringLiteral("Fact")));
    m_factInput = new QLineEdit();
    m_factInput->setMaximumWidth(80);
    m_factInput->setFont(QFont(QStringLiteral("Arial"), 12));
    factRow->addWidget(m_factInput);
    auto* factEq = new QPushButton(QStringLiteral("="));
    factEq->setStyleSheet(opBtnStyle());
    factEq->setFocusPolicy(Qt::NoFocus);
    connect(factEq, &QPushButton::clicked, this, &ScientificCalculator::onFactCalc);
    factRow->addWidget(factEq);
    auto* factClear = new QPushButton(QStringLiteral("Clear"));
    factClear->setFocusPolicy(Qt::NoFocus);
    connect(factClear, &QPushButton::clicked, m_factInput, &QLineEdit::clear);
    factRow->addWidget(factClear);
    m_factResult = new QTextBrowser();
    m_factResult->setMaximumHeight(50);
    factRow->addWidget(m_factResult);
    layout->addLayout(factRow);

    // Permutation
    auto* permRow = new QHBoxLayout();
    permRow->addWidget(new QLabel(QStringLiteral("A(n,m)")));
    m_permA1 = new QLineEdit();
    m_permA1->setMaximumWidth(60);
    m_permA1->setPlaceholderText(QStringLiteral("n"));
    m_permA1->setFont(QFont(QStringLiteral("Arial"), 12));
    permRow->addWidget(m_permA1);
    m_permA2 = new QLineEdit();
    m_permA2->setMaximumWidth(60);
    m_permA2->setPlaceholderText(QStringLiteral("m"));
    m_permA2->setFont(QFont(QStringLiteral("Arial"), 12));
    permRow->addWidget(m_permA2);
    auto* permEq = new QPushButton(QStringLiteral("="));
    permEq->setStyleSheet(opBtnStyle());
    permEq->setFocusPolicy(Qt::NoFocus);
    connect(permEq, &QPushButton::clicked, this, &ScientificCalculator::onPermCalc);
    permRow->addWidget(permEq);
    auto* permClear = new QPushButton(QStringLiteral("Clear"));
    permClear->setFocusPolicy(Qt::NoFocus);
    connect(permClear, &QPushButton::clicked, m_permA1, &QLineEdit::clear);
    connect(permClear, &QPushButton::clicked, m_permA2, &QLineEdit::clear);
    permRow->addWidget(permClear);
    m_permResult = new QTextBrowser();
    m_permResult->setMaximumHeight(50);
    permRow->addWidget(m_permResult);
    layout->addLayout(permRow);

    // Combination
    auto* combRow = new QHBoxLayout();
    combRow->addWidget(new QLabel(QStringLiteral("C(n,m)")));
    m_combC1 = new QLineEdit();
    m_combC1->setMaximumWidth(60);
    m_combC1->setPlaceholderText(QStringLiteral("n"));
    m_combC1->setFont(QFont(QStringLiteral("Arial"), 12));
    combRow->addWidget(m_combC1);
    m_combC2 = new QLineEdit();
    m_combC2->setMaximumWidth(60);
    m_combC2->setPlaceholderText(QStringLiteral("m"));
    m_combC2->setFont(QFont(QStringLiteral("Arial"), 12));
    combRow->addWidget(m_combC2);
    auto* combEq = new QPushButton(QStringLiteral("="));
    combEq->setStyleSheet(opBtnStyle());
    combEq->setFocusPolicy(Qt::NoFocus);
    connect(combEq, &QPushButton::clicked, this, &ScientificCalculator::onCombCalc);
    combRow->addWidget(combEq);
    auto* combClear = new QPushButton(QStringLiteral("Clear"));
    combClear->setFocusPolicy(Qt::NoFocus);
    connect(combClear, &QPushButton::clicked, m_combC1, &QLineEdit::clear);
    connect(combClear, &QPushButton::clicked, m_combC2, &QLineEdit::clear);
    combRow->addWidget(combClear);
    m_combResult = new QTextBrowser();
    m_combResult->setMaximumHeight(50);
    combRow->addWidget(m_combResult);
    layout->addLayout(combRow);

    // Number pad
    auto* grid = new QGridLayout();
    grid->setSpacing(4);
    auto addDigit = [&](const QString& text, int row, int col) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(digitBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onPermDigit);
        grid->addWidget(btn, row, col);
    };
    addDigit(QStringLiteral("7"), 0, 0);
    addDigit(QStringLiteral("8"), 0, 1);
    addDigit(QStringLiteral("9"), 0, 2);
    addDigit(QStringLiteral("4"), 1, 0);
    addDigit(QStringLiteral("5"), 1, 1);
    addDigit(QStringLiteral("6"), 1, 2);
    addDigit(QStringLiteral("1"), 2, 0);
    addDigit(QStringLiteral("2"), 2, 1);
    addDigit(QStringLiteral("3"), 2, 2);
    addDigit(QStringLiteral("0"), 3, 0);
    auto* delBtn = new QPushButton(QStringLiteral("<-"));
    delBtn->setStyleSheet(opBtnStyle());
    delBtn->setFocusPolicy(Qt::NoFocus);
    connect(delBtn, &QPushButton::clicked, this, &ScientificCalculator::onPermDelete);
    grid->addWidget(delBtn, 3, 2);
    layout->addLayout(grid);

    return tab;
}

// ============================================================================
// Complex Tab
// ============================================================================

QWidget* ScientificCalculator::createComplexTab()
{
    auto* tab = new QWidget();
    auto* layout = new QVBoxLayout(tab);

    auto* row1 = new QHBoxLayout();
    m_cxReal1 = new QLineEdit();
    m_cxReal1->setPlaceholderText(QStringLiteral("Real1"));
    m_cxReal1->setMaximumWidth(100);
    m_cxReal1->setFont(QFont(QStringLiteral("Arial"), 10));
    m_cxImag1 = new QLineEdit();
    m_cxImag1->setPlaceholderText(QStringLiteral("Imag1"));
    m_cxImag1->setMaximumWidth(100);
    m_cxImag1->setFont(QFont(QStringLiteral("Arial"), 10));
    row1->addWidget(m_cxReal1);
    row1->addWidget(new QLabel(QStringLiteral("+")));
    row1->addWidget(m_cxImag1);
    row1->addWidget(new QLabel(QStringLiteral("i")));
    layout->addLayout(row1);

    auto* opRow = new QHBoxLayout();
    m_cxOp = new QComboBox();
    m_cxOp->addItems({QStringLiteral("+"), QStringLiteral("-"),
                      QStringLiteral("*"), QStringLiteral("/")});
    m_cxOp->setMaximumWidth(60);
    opRow->addWidget(m_cxOp);
    opRow->addStretch();
    layout->addLayout(opRow);

    auto* row2 = new QHBoxLayout();
    m_cxReal2 = new QLineEdit();
    m_cxReal2->setPlaceholderText(QStringLiteral("Real2"));
    m_cxReal2->setMaximumWidth(100);
    m_cxReal2->setFont(QFont(QStringLiteral("Arial"), 10));
    m_cxImag2 = new QLineEdit();
    m_cxImag2->setPlaceholderText(QStringLiteral("Imag2"));
    m_cxImag2->setMaximumWidth(100);
    m_cxImag2->setFont(QFont(QStringLiteral("Arial"), 10));
    row2->addWidget(m_cxReal2);
    row2->addWidget(new QLabel(QStringLiteral("+")));
    row2->addWidget(m_cxImag2);
    row2->addWidget(new QLabel(QStringLiteral("i")));
    layout->addLayout(row2);

    auto* resRow = new QHBoxLayout();
    resRow->addWidget(new QLabel(QStringLiteral("Result")));
    m_cxResult = new QLineEdit();
    m_cxResult->setReadOnly(true);
    m_cxResult->setFont(QFont(QStringLiteral("Arial"), 12));
    resRow->addWidget(m_cxResult);
    layout->addLayout(resRow);

    auto* btnRow = new QHBoxLayout();
    auto* clearBtn = new QPushButton(QStringLiteral("C"));
    clearBtn->setStyleSheet(opBtnStyle());
    clearBtn->setFocusPolicy(Qt::NoFocus);
    connect(clearBtn, &QPushButton::clicked, this, &ScientificCalculator::onCxClear);
    btnRow->addWidget(clearBtn);
    auto* backBtn = new QPushButton(QStringLiteral("<-"));
    backBtn->setStyleSheet(opBtnStyle());
    backBtn->setFocusPolicy(Qt::NoFocus);
    connect(backBtn, &QPushButton::clicked, this, &ScientificCalculator::onCxBack);
    btnRow->addWidget(backBtn);
    auto* eqBtn = new QPushButton(QStringLiteral("="));
    eqBtn->setStyleSheet(opBtnStyle());
    eqBtn->setFocusPolicy(Qt::NoFocus);
    connect(eqBtn, &QPushButton::clicked, this, &ScientificCalculator::onCxEqual);
    btnRow->addWidget(eqBtn);
    layout->addLayout(btnRow);

    auto* grid = new QGridLayout();
    grid->setSpacing(4);
    auto addDigit = [&](const QString& text, int row, int col) {
        auto* btn = new QPushButton(text);
        btn->setStyleSheet(digitBtnStyle());
        btn->setFocusPolicy(Qt::NoFocus);
        connect(btn, &QPushButton::clicked, this, &ScientificCalculator::onCxDigit);
        grid->addWidget(btn, row, col);
    };
    addDigit(QStringLiteral("7"), 0, 0);
    addDigit(QStringLiteral("8"), 0, 1);
    addDigit(QStringLiteral("9"), 0, 2);
    addDigit(QStringLiteral("4"), 1, 0);
    addDigit(QStringLiteral("5"), 1, 1);
    addDigit(QStringLiteral("6"), 1, 2);
    addDigit(QStringLiteral("1"), 2, 0);
    addDigit(QStringLiteral("2"), 2, 1);
    addDigit(QStringLiteral("3"), 2, 2);
    addDigit(QStringLiteral("0"), 3, 0);
    addDigit(QStringLiteral("."), 3, 1);
    auto* negBtn = new QPushButton(QStringLiteral("-"));
    negBtn->setStyleSheet(opBtnStyle());
    negBtn->setFocusPolicy(Qt::NoFocus);
    connect(negBtn, &QPushButton::clicked, this, &ScientificCalculator::onCxDigit);
    grid->addWidget(negBtn, 3, 2);
    layout->addLayout(grid);

    return tab;
}

// ============================================================================
// Standard Button Handler
// ============================================================================

void ScientificCalculator::onStdButton()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString text = btn->text();
    QString input = m_stdInput->text();
    QChar pi = piChar();

    if (text == QStringLiteral("C")) {
        m_stdExpr.clear();
        m_stdExprCalc.clear();
        m_stdInput->clear();
        m_stdResult->clear();
        m_stdPointFlag = true;
        return;
    }
    if (text == QStringLiteral("<-")) {
        if (refuseCompositeFunction(m_stdExpr)) {
            if (!m_stdExpr.isEmpty() && m_stdExpr.right(1) == QStringLiteral("."))
                m_stdPointFlag = true;
            m_stdExpr.chop(1);
            m_stdExprCalc.chop(1);
            m_stdInput->setText(m_stdExpr);
        }
        return;
    }
    if (text == QStringLiteral("+") || text == QStringLiteral("-") || text == QStringLiteral("*") ||
        text == QStringLiteral("/") || text == QStringLiteral("^")) {
        if (!m_stdExpr.isEmpty()) {
            QChar last = m_stdExpr.right(1).at(0);
            if (last.isDigit() || last == QChar(')')) {
                m_stdExpr += text;
                m_stdExprCalc += text;
                m_stdInput->setText(m_stdExpr);
                m_stdPointFlag = true;
            }
        }
        return;
    }
    if (text == QStringLiteral(".")) {
        if (!m_stdExpr.isEmpty() && m_stdExpr.right(1).at(0).isDigit() && m_stdPointFlag) {
            m_stdExpr += QStringLiteral(".");
            m_stdExprCalc += QStringLiteral(".");
            m_stdInput->setText(m_stdExpr);
            m_stdPointFlag = false;
        }
        return;
    }
    if (text == QStringLiteral("(")) {
        if (m_stdExpr.isEmpty() || (!m_stdExpr.right(1).at(0).isDigit() &&
            m_stdExpr.right(1) != QStringLiteral(".") && m_stdExpr.right(1) != QStringLiteral(")"))) {
            m_stdExpr += QStringLiteral("(");
            m_stdExprCalc += QStringLiteral("(");
            m_stdInput->setText(m_stdExpr);
            m_stdPointFlag = true;
        }
        return;
    }
    if (text == QStringLiteral(")")) {
        if (!m_stdExpr.isEmpty()) {
            QChar last = m_stdExpr.right(1).at(0);
            if (last.isDigit() || last == QChar(')')) {
                m_stdExpr += QStringLiteral(")");
                m_stdExprCalc += QStringLiteral(")");
                m_stdInput->setText(m_stdExpr);
                m_stdPointFlag = true;
            }
        }
        return;
    }
    if (text == QString::fromUtf8("\xe2\x88\x9a")) { // sqrt
        if (refuseCompositeFunction(m_stdExpr) && !m_stdExpr.isEmpty() &&
            m_stdExpr.right(1).at(0).isDigit()) {
            int count = 0;
            for (int i = m_stdExpr.size() - 1; i >= 0; --i) {
                if (m_stdExpr.at(i).isDigit() || m_stdExpr.at(i) == QChar('.')) count++;
                else break;
            }
            QString numStr = m_stdExpr.right(count);
            double sqrtVal = std::sqrt(numStr.toDouble());
            m_stdExprCalc = m_stdExprCalc.left(m_stdExprCalc.size() - count) +
                            QString::number(sqrtVal, 'g', 10);
            m_stdExpr = m_stdExpr.left(m_stdExpr.size() - count) +
                        QString::fromUtf8("\xe2\x88\x9a") + numStr;
            m_stdInput->setText(m_stdExpr);
        }
        return;
    }
    if (text == QStringLiteral("=")) {
        if (m_stdExpr.isEmpty()) return;
        QChar last = m_stdExpr.right(1).at(0);
        if (!last.isDigit() && last != QChar(')')) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Incomplete expression."));
            m_stdResult->clear();
            return;
        }
        int lc = 0, rc = 0;
        for (const QChar& c : m_stdExpr) {
            if (c == QChar('(')) lc++;
            else if (c == QChar(')')) rc++;
        }
        if (lc != rc) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Mismatched parentheses."));
            m_stdResult->clear();
            return;
        }
        std::string str = m_stdExprCalc.toStdString();
        char OPS[256];
        int len = 0;
        double result = 0;
        bool zf = true;
        infixToSuffix(str.c_str(), OPS, len);
        calculateSuffix(OPS, len, result, zf);
        if (!zf) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Division by zero."));
            m_stdResult->clear();
        } else {
            m_stdResult->setText(QString::number(result, 'g', 10));
        }
        return;
    }
    // Digit
    if (refuseCompositeFunction(m_stdExpr) && m_stdExpr.right(1) != QStringLiteral(")")) {
        m_stdExprCalc += text;
        m_stdExpr += text;
        m_stdInput->setText(m_stdExpr);
    }
}

// ============================================================================
// Scientific Button Handler
// ============================================================================

void ScientificCalculator::onSciButton()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString text = btn->text();
    QChar pi = piChar();

    auto isOpEnd = [&](const QString& e) -> bool {
        if (e.isEmpty()) return false;
        QChar last = e.right(1).at(0);
        return last.isDigit() || last == QChar(')') || last == QChar('|') ||
               last == QChar('e') || last == pi;
    };

    if (text == QStringLiteral("C")) {
        m_sciExpr.clear();
        m_sciExprCalc.clear();
        m_sciInput->clear();
        m_sciResult->clear();
        m_sciPointFlag = true;
        return;
    }
    if (text == QStringLiteral("<-")) {
        if (refuseCompositeFunction(m_sciExpr)) {
            if (m_sciExpr.size() >= 3 && m_sciExpr.right(3) == QStringLiteral("mod")) {
                m_sciExpr.chop(3);
                m_sciExprCalc.chop(1);
            } else if (m_sciExpr.right(1).at(0) == pi || m_sciExpr.right(1) == QStringLiteral("e")) {
                m_sciExpr.chop(1);
                m_sciExprCalc.chop(8);
            } else {
                if (!m_sciExpr.isEmpty() && m_sciExpr.right(1) == QStringLiteral("."))
                    m_sciPointFlag = true;
                m_sciExpr.chop(1);
                m_sciExprCalc.chop(1);
            }
            m_sciInput->setText(m_sciExpr);
        }
        return;
    }
    // Constants
    if (text == QStringLiteral("pi")) {
        if (m_sciExpr.isEmpty() || (!m_sciExpr.right(1).at(0).isDigit() &&
            m_sciExpr.right(1) != QStringLiteral(")") &&
            m_sciExpr.right(1) != QStringLiteral("e") &&
            m_sciExpr.right(1).at(0) != pi)) {
            m_sciExpr += pi;
            m_sciExprCalc += QStringLiteral("3.141593");
            m_sciInput->setText(m_sciExpr);
            m_sciPointFlag = false;
        }
        return;
    }
    if (text == QStringLiteral("e")) {
        if (m_sciExpr.isEmpty() || (!m_sciExpr.right(1).at(0).isDigit() &&
            m_sciExpr.right(1) != QStringLiteral(")") &&
            m_sciExpr.right(1) != QStringLiteral("e") &&
            m_sciExpr.right(1).at(0) != pi)) {
            m_sciExpr += QStringLiteral("e");
            m_sciExprCalc += QStringLiteral("2.718282");
            m_sciInput->setText(m_sciExpr);
            m_sciPointFlag = false;
        }
        return;
    }
    // Binary operators
    if (text == QStringLiteral("+") || text == QStringLiteral("-") || text == QStringLiteral("*") ||
        text == QStringLiteral("/") || text == QStringLiteral("^")) {
        if (isOpEnd(m_sciExpr)) {
            m_sciExpr += text;
            m_sciExprCalc += text;
            m_sciInput->setText(m_sciExpr);
            m_sciPointFlag = true;
        }
        return;
    }
    if (text == QStringLiteral("mod")) {
        if (isOpEnd(m_sciExpr)) {
            m_sciExpr += QStringLiteral("mod");
            m_sciExprCalc += QStringLiteral("%");
            m_sciInput->setText(m_sciExpr);
            m_sciPointFlag = true;
        }
        return;
    }
    if (text == QStringLiteral(".")) {
        if (!m_sciExpr.isEmpty() && m_sciExpr.right(1).at(0).isDigit() && m_sciPointFlag) {
            m_sciExpr += QStringLiteral(".");
            m_sciExprCalc += QStringLiteral(".");
            m_sciInput->setText(m_sciExpr);
            m_sciPointFlag = false;
        }
        return;
    }
    if (text == QStringLiteral("(")) {
        if (m_sciExpr.isEmpty() || (!m_sciExpr.right(1).at(0).isDigit() &&
            m_sciExpr.right(1) != QStringLiteral(".") && m_sciExpr.right(1) != QStringLiteral(")") &&
            m_sciExpr.right(1) != QStringLiteral("e") && m_sciExpr.right(1).at(0) != pi)) {
            m_sciExpr += QStringLiteral("(");
            m_sciExprCalc += QStringLiteral("(");
            m_sciInput->setText(m_sciExpr);
            m_sciPointFlag = true;
        }
        return;
    }
    if (text == QStringLiteral(")")) {
        if (isOpEnd(m_sciExpr)) {
            m_sciExpr += QStringLiteral(")");
            m_sciExprCalc += QStringLiteral(")");
            m_sciInput->setText(m_sciExpr);
            m_sciPointFlag = true;
        }
        return;
    }
    // Unary functions
    auto applyUnary = [&](const QString& displayPrefix, auto func) {
        if (!refuseCompositeFunction(m_sciExpr) || !isOpEnd(m_sciExpr)) return;
        int count = 0;
        bool isConst = false;
        QString constName;
        if (m_sciExpr.right(1).at(0) == pi) {
            isConst = true;
            constName = QString(pi);
            count = 1;
        } else if (m_sciExpr.right(1) == QStringLiteral("e") &&
                   (m_sciExpr.size() == 1 || !m_sciExpr.right(2).left(1).at(0).isLetter())) {
            isConst = true;
            constName = QStringLiteral("e");
            count = 1;
        } else {
            for (int i = m_sciExpr.size() - 1; i >= 0; --i) {
                if (m_sciExpr.at(i).isDigit() || m_sciExpr.at(i) == QChar('.')) count++;
                else break;
            }
        }
        if (count == 0 && !isConst) return;
        double val = isConst ? ((constName.at(0) == pi) ? 3.141593 : 2.718282)
                             : m_sciExpr.right(count).toDouble();
        double res = func(val);
        m_sciExprCalc = m_sciExprCalc.left(m_sciExprCalc.size() - (isConst ? 8 : count)) +
                        QString::number(res, 'g', 10);
        m_sciExpr = m_sciExpr.left(m_sciExpr.size() - count) +
                    displayPrefix + (isConst ? constName : m_sciExpr.right(count));
        m_sciInput->setText(m_sciExpr);
    };

    if (text == QString::fromUtf8("\xe2\x88\x9a")) {
        applyUnary(text, [](double v) { return std::sqrt(v); });
        return;
    }
    if (text == QStringLiteral("1/x")) {
        applyUnary(text, [this](double v) {
            if (v == 0.0) { QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Division by zero.")); return 0.0; }
            return 1.0 / v;
        });
        return;
    }
    if (text == QStringLiteral("|x|")) {
        applyUnary(text, [](double v) { return std::fabs(v); });
        return;
    }
    if (text == QStringLiteral("exp")) {
        applyUnary(text, [](double v) { return std::exp(v); });
        return;
    }
    if (text == QStringLiteral("log")) {
        applyUnary(text, [this](double v) {
            if (v == 0.0) { QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Division by zero.")); return 0.0; }
            return std::log10(v);
        });
        return;
    }
    if (text == QStringLiteral("ln")) {
        applyUnary(text, [this](double v) {
            if (v == 0.0) { QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Division by zero.")); return 0.0; }
            return std::log(v);
        });
        return;
    }
    if (text == QStringLiteral("sin")) {
        applyUnary(text, [](double v) { return std::sin(v); });
        return;
    }
    if (text == QStringLiteral("cos")) {
        applyUnary(text, [](double v) { return std::cos(v); });
        return;
    }
    // Equals
    if (text == QStringLiteral("=")) {
        if (m_sciExpr.isEmpty()) return;
        QChar last = m_sciExpr.right(1).at(0);
        if (!isOpEnd(m_sciExpr)) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Incomplete expression."));
            m_sciResult->clear();
            return;
        }
        int lc = 0, rc = 0;
        for (const QChar& c : m_sciExpr) {
            if (c == QChar('(')) lc++;
            else if (c == QChar(')')) rc++;
        }
        if (lc != rc) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Mismatched parentheses."));
            m_sciResult->clear();
            return;
        }
        std::string str = m_sciExprCalc.toStdString();
        char OPS[256];
        int len = 0;
        double result = 0;
        bool zf = true;
        infixToSuffix(str.c_str(), OPS, len);
        calculateSuffix(OPS, len, result, zf);
        if (!zf) {
            QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Division by zero."));
            m_sciResult->clear();
        } else {
            m_sciResult->setText(QString::number(result, 'g', 10));
        }
        return;
    }
    // Digits 0-9
    if (refuseCompositeFunction(m_sciExpr) && m_sciExpr.right(1) != QStringLiteral("e") &&
        m_sciExpr.right(1).at(0) != pi && m_sciExpr.right(1) != QStringLiteral(")")) {
        m_sciExprCalc += text;
        m_sciExpr += text;
        m_sciInput->setText(m_sciExpr);
    }
}

// ============================================================================
// Permutation/Combination
// ============================================================================

void ScientificCalculator::onPermDigit()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString d = btn->text();
    if (m_permA1->hasFocus()) m_permA1->setText(m_permA1->text() + d);
    else if (m_permA2->hasFocus()) m_permA2->setText(m_permA2->text() + d);
    else if (m_combC1->hasFocus()) m_combC1->setText(m_combC1->text() + d);
    else if (m_combC2->hasFocus()) m_combC2->setText(m_combC2->text() + d);
    else if (m_factInput->hasFocus()) m_factInput->setText(m_factInput->text() + d);
}

void ScientificCalculator::onPermDelete()
{
    QLineEdit* f = nullptr;
    if (m_permA1->hasFocus()) f = m_permA1;
    else if (m_permA2->hasFocus()) f = m_permA2;
    else if (m_combC1->hasFocus()) f = m_combC1;
    else if (m_combC2->hasFocus()) f = m_combC2;
    else if (m_factInput->hasFocus()) f = m_factInput;
    if (f) {
        QString t = f->text();
        t.chop(1);
        f->setText(t);
    }
}

void ScientificCalculator::onCombCalc()
{
    int n = m_combC1->text().toInt();
    int m = m_combC2->text().toInt();
    if (m > n || m <= 0 || n <= 0) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Invalid input."));
        return;
    }
    if (n > 3000) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Max n=3000."));
        return;
    }
    m_combResult->setText(QString::number(combinate(n, m)));
}

void ScientificCalculator::onPermCalc()
{
    int n = m_permA1->text().toInt();
    int m = m_permA2->text().toInt();
    if (m > n || m <= 0 || n <= 0) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Invalid input."));
        return;
    }
    if (n > 25) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Max n=25."));
        return;
    }
    m_permResult->setText(QString::number(arrange(n, m)));
}

void ScientificCalculator::onFactCalc()
{
    QString s = m_factInput->text();
    int n = s.toInt();
    if ((s != QStringLiteral("0") && n == 0) || n < 0) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Invalid input."));
        return;
    }
    if (n > 5000) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Max factorial=5000."));
        return;
    }
    int digits[20001] = {0};
    int len = 1;
    digits[0] = 1;
    for (int i = 2; i <= n; ++i) {
        int carry = 0;
        for (int j = 0; j < len; ++j) {
            int prod = digits[j] * i + carry;
            digits[j] = prod % 10;
            carry = prod / 10;
        }
        while (carry) {
            digits[len++] = carry % 10;
            carry /= 10;
        }
    }
    QString result;
    for (int i = len - 1; i >= 0; --i) result += QString::number(digits[i]);
    m_factResult->setText(result);
}

// ============================================================================
// Complex
// ============================================================================

void ScientificCalculator::onCxDigit()
{
    auto* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString d = btn->text();
    if (m_cxReal1->hasFocus()) m_cxReal1->setText(m_cxReal1->text() + d);
    else if (m_cxImag1->hasFocus()) m_cxImag1->setText(m_cxImag1->text() + d);
    else if (m_cxReal2->hasFocus()) m_cxReal2->setText(m_cxReal2->text() + d);
    else if (m_cxImag2->hasFocus()) m_cxImag2->setText(m_cxImag2->text() + d);
}

void ScientificCalculator::onCxClear()
{
    m_cxReal1->clear();
    m_cxImag1->clear();
    m_cxReal2->clear();
    m_cxImag2->clear();
    m_cxResult->clear();
}

void ScientificCalculator::onCxBack()
{
    QLineEdit* f = nullptr;
    if (m_cxReal1->hasFocus()) f = m_cxReal1;
    else if (m_cxImag1->hasFocus()) f = m_cxImag1;
    else if (m_cxReal2->hasFocus()) f = m_cxReal2;
    else if (m_cxImag2->hasFocus()) f = m_cxImag2;
    if (f) {
        QString t = f->text();
        t.chop(1);
        f->setText(t);
    }
}

void ScientificCalculator::onCxEqual()
{
    QString r1s = m_cxReal1->text(), i1s = m_cxImag1->text();
    QString r2s = m_cxReal2->text(), i2s = m_cxImag2->text();
    if (r1s.isEmpty() || i1s.isEmpty() || r2s.isEmpty() || i2s.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("All fields required."));
        onCxClear();
        return;
    }
    float r1 = r1s.toFloat(), i1 = i1s.toFloat();
    float r2 = r2s.toFloat(), i2 = i2s.toFloat();
    if ((r1 == 0 && r1s != QStringLiteral("0")) || (i1 == 0 && i1s != QStringLiteral("0")) ||
        (r2 == 0 && r2s != QStringLiteral("0")) || (i2 == 0 && i2s != QStringLiteral("0"))) {
        QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Non-numeric input."));
        onCxClear();
        return;
    }
    QChar op = m_cxOp->currentText().at(0);
    float rr = 0, ir = 0;
    switch (op.unicode()) {
    case '+': rr = r1 + r2; ir = i1 + i2; break;
    case '-': rr = r1 - r2; ir = i1 - i2; break;
    case '*': rr = r1*r2 - i1*i2; ir = i1*r2 + r1*i2; break;
    case '/': {
        float d = r2*r2 + i2*i2;
        if (d == 0.0f) { QMessageBox::warning(this, QStringLiteral("Warning"), QStringLiteral("Division by zero.")); onCxClear(); return; }
        rr = (r1*r2 + i1*i2) / d;
        ir = (i1*r2 - r1*i2) / d;
        break;
    }
    }
    m_cxResult->setText(ir >= 0 ? QStringLiteral("%1+%2i").arg(rr).arg(ir)
                                : QStringLiteral("%1%2i").arg(rr).arg(ir));
}

// ============================================================================
// Shared: Infix to Suffix + Calculate
// ============================================================================

bool ScientificCalculator::refuseCompositeFunction(const QString& inp)
{
    if (inp.isEmpty()) return true;
    QChar pi = piChar();
    int i = inp.size() - 1;
    while (i >= 0) {
        QChar c = inp.at(i);
        if (c.isDigit() || c == QChar('.') || c == QChar('e') || c == pi || c == QChar(')'))
            i--;
        else break;
    }
    if (i < 0) return true;
    QChar c = inp.at(i);
    return (c == QChar('+') || c == QChar('-') || c == QChar('*') || c == QChar('/') ||
            c == QChar('^') || c == QChar('(') || c == QChar('d'));
}

void ScientificCalculator::infixToSuffix(const char* S, char OPS[], int& len)
{
    QStack<char> ope;
    unsigned int j = 0;
    unsigned int tmp = static_cast<unsigned int>(strlen(S));
    for (unsigned int i = 0; i < tmp; i++) {
        switch (S[i]) {
        case '+':
        case '-':
            if (S[i] == '-' && (i == 0 || S[i - 1] == '(')) {
                while ((S[i] >= '0' && S[i] <= '9') || S[i] == '.' ||
                       (S[i] == '-' && (i == 0 || S[i - 1] < '0' || S[i - 1] > '9'))) {
                    OPS[j++] = S[i];
                    if (S[i] == '-') OPS[j++] = '@';
                    i++;
                }
                i--;
                OPS[j++] = '#';
            } else {
                if (ope.isEmpty()) ope.push(S[i]);
                else if (ope.top() == '*' || ope.top() == '/' || ope.top() == '%' ||
                         ope.top() == '^' || ope.top() == '+' || ope.top() == '-') {
                    OPS[j++] = ope.pop();
                    i--;
                } else ope.push(S[i]);
            }
            break;
        case '^': ope.push(S[i]); break;
        case '*': case '/': case '%':
            if (ope.isEmpty()) ope.push(S[i]);
            else if (ope.top() == '^' || ope.top() == S[i] ||
                     (S[i] != '^' && (ope.top() == '*' || ope.top() == '/' || ope.top() == '%'))) {
                OPS[j++] = ope.pop();
                i--;
            } else ope.push(S[i]);
            break;
        case '(': ope.push(S[i]); break;
        case ')':
            while (!ope.isEmpty() && ope.top() != '(') OPS[j++] = ope.pop();
            if (!ope.isEmpty()) ope.pop();
            break;
        default:
            while ((S[i] >= '0' && S[i] <= '9') || S[i] == '.' ||
                   (S[i] == '-' && (i == 0 || S[i - 1] < '0' || S[i - 1] > '9'))) {
                OPS[j++] = S[i];
                i++;
            }
            i--;
            OPS[j++] = '#';
            break;
        }
    }
    while (!ope.isEmpty()) OPS[j++] = ope.pop();
    len = static_cast<int>(j);
}

void ScientificCalculator::calculateSuffix(char SUF[], int len, double& result, bool& flag)
{
    QStack<double> st;
    for (int i = 0; i < len; i++) {
        switch (SUF[i]) {
        case '^': { double a = st.pop(), b = st.pop(); st.push(std::pow(b, a)); break; }
        case '+': { double a = st.pop(), b = st.pop(); st.push(b + a); break; }
        case '-':
            if (i + 1 < len && SUF[i + 1] == '@') {
                int jx = 0;
                char stx[32];
                i++;
                while (i < len && SUF[i] != '#') {
                    if (SUF[i] != '@') stx[jx++] = SUF[i];
                    i++;
                }
                stx[jx] = '\0';
                st.push(std::atof(stx));
            } else {
                double a = st.pop(), b = st.pop();
                st.push(b - a);
            }
            break;
        case '*': { double a = st.pop(), b = st.pop(); st.push(b * a); break; }
        case '/': {
            double a = st.pop(), b = st.pop();
            if (a == 0) { flag = false; return; }
            st.push(b / a);
            break;
        }
        case '%': {
            double a = st.pop(), b = st.pop();
            if (a == 0) { flag = false; return; }
            st.push(std::fmod(b, a));
            break;
        }
        default: {
            int j = 0;
            char numBuf[32];
            while (i < len && SUF[i] != '#') numBuf[j++] = SUF[i++];
            numBuf[j] = '\0';
            st.push(std::atof(numBuf));
            break;
        }
        }
    }
    result = st.top();
}

// ============================================================================
// Permutation/Combination Math
// ============================================================================

QVector<int> ScientificCalculator::primProduce()
{
    QVector<int> vc;
    vc.push_back(2);
    for (int i = 3; i * i <= MAXN; i += 2) {
        if (!sieveStorage[i]) {
            vc.push_back(i);
            for (int j = i * i; j <= MAXN; j += i) sieveStorage[j] = true;
        }
    }
    return vc;
}

int ScientificCalculator::cal(int x, int p)
{
    int ans = 0;
    long long re = p;
    while (x >= re) { ans += x / static_cast<int>(re); re *= p; }
    return ans;
}

int ScientificCalculator::powMod(long long n, int k)
{
    long long ans = 1;
    while (k) {
        if (k & 1) ans = ans * n % MOD;
        n = (n * n) % MOD;
        k >>= 1;
    }
    return static_cast<int>(ans);
}

long long ScientificCalculator::combinate(int n, int m)
{
    QVector<int> prim = primProduce();
    long long ans = 1;
    for (int i = 0; i < prim.size() && prim[i] <= n; ++i) {
        int num = cal(n, prim[i]) - cal(m, prim[i]) - cal(n - m, prim[i]);
        ans = (ans * powMod(prim[i], num)) % MOD;
    }
    return ans;
}

long long ScientificCalculator::arrange(int n, int m)
{
    long long res = m;
    for (int i = 1; i < n; ++i) res *= (--m);
    return res;
}

} // namespace ks
