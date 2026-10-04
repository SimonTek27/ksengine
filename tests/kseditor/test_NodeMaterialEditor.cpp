#include <QtTest/QtTest>
#include <QJsonDocument>
#include "sdk/kseditor/modules/modellingEditor/NodeMaterialEditor.h"

class TestNodeMaterialEditor : public QObject {
    Q_OBJECT
private slots:
    void createAndConnectNodes();
    void serializeRoundTrip();
    void generateShaderHasFragColor();
    void availableTypesComplete();
    void deleteNodeUpdatesLinks();
    void texturePathPersists();
};

void TestNodeMaterialEditor::createAndConnectNodes()
{
    ks::MaterialGraph graph;
    ks::MaterialNode* bsdf = graph.createNode("PrincipledBSDF", QPointF(100, 50));
    QVERIFY(bsdf);
    QCOMPARE(bsdf->factoryType, QString("PrincipledBSDF"));
    QCOMPARE(bsdf->inputs.size(), 8);
    QCOMPARE(bsdf->outputs.size(), 1);

    ks::MaterialNode* output = graph.createNode("Output", QPointF(400, 50));
    QVERIFY(output);
    QVERIFY(graph.outputNode == output);

    graph.connectNodes(bsdf->id, bsdf->outputs[0].id, output->id, output->inputs[0].id);

    QVERIFY(output->inputs[0].isConnected);
    QCOMPARE(output->inputs[0].connectedSocketId, bsdf->outputs[0].id);
    QVERIFY(bsdf->outputs[0].isConnected);
    QVERIFY(bsdf->linkedNodes.contains(bsdf->outputs[0].id));
    QVERIFY(bsdf->linkedNodes[bsdf->outputs[0].id] == output);
}

void TestNodeMaterialEditor::serializeRoundTrip()
{
    ks::MaterialGraph graph;
    ks::MaterialNode* bsdf = graph.createNode("PrincipledBSDF", QPointF(120, 80));
    ks::MaterialNode* mix = graph.createNode("Mix", QPointF(-80, 40));
    ks::MaterialNode* output = graph.createNode("Output", QPointF(420, 90));

    bsdf->inputs[0].value = QColor(200, 60, 40, 255);
    bsdf->inputs[1].value = 0.35;
    bsdf->inputs[2].value = 0.7;
    mix->inputs[0].value = 0.5;

    graph.connectNodes(mix->id, mix->outputs[0].id, bsdf->id, bsdf->inputs[0].id);
    graph.connectNodes(bsdf->id, bsdf->outputs[0].id, output->id, output->inputs[0].id);

    const QJsonObject json = graph.toJson();
    QCOMPARE(json["nodes"].toArray().size(), 3);
    QCOMPARE(json["connections"].toArray().size(), 2);

    ks::MaterialGraph restored;
    restored.fromJson(json);
    QCOMPARE(restored.nodes.size(), 3);
    QVERIFY(restored.outputNode != nullptr);

    ks::MaterialNode* rbsdf = nullptr;
    for (auto* n : restored.nodes)
        if (n->id == bsdf->id) rbsdf = n;
    QVERIFY(rbsdf);
    QCOMPARE(rbsdf->position.x(), 120.0);
    QVERIFY(qAbs(rbsdf->inputs[1].value.toDouble() - 0.35) < 1e-6);
    QVERIFY(rbsdf->inputs[0].value.value<QColor>().green() == 60);
    QVERIFY(rbsdf->inputs[0].isConnected);

    // Connection references round-trip: restored input connected socket ids match.
    ks::MaterialNode* rout = restored.outputNode;
    QVERIFY(rout->inputs[0].isConnected);
    QCOMPARE(rout->inputs[0].connectedSocketId, bsdf->outputs[0].id);
}

void TestNodeMaterialEditor::generateShaderHasFragColor()
{
    ks::MaterialGraph graph;
    ks::MaterialNode* bsdf = graph.createNode("PrincipledBSDF", QPointF(100, 100));
    ks::MaterialNode* output = graph.createNode("Output", QPointF(400, 100));
    graph.connectNodes(bsdf->id, bsdf->outputs[0].id, output->id, output->inputs[0].id);

    const QString glsl = graph.generateGLSL();
    QVERIFY(glsl.contains("#version 330 core"));
    QVERIFY(glsl.contains("void main()"));
    QVERIFY(glsl.contains("fragColor"));
}

void TestNodeMaterialEditor::availableTypesComplete()
{
    ks::MaterialNodeEditor editor;
    editor.registerDefaultNodes();
    const QStringList types = editor.getAvailableNodeTypes();
    QVERIFY(types.contains("Output"));
    QVERIFY(types.contains("PrincipledBSDF"));
    QVERIFY(types.contains("ImageTexture"));
    QVERIFY(types.contains("Math"));
    QVERIFY(types.contains("NoiseTexture"));
    QVERIFY(types.contains("NormalMap"));
    QVERIFY(types.size() >= 26);
}

void TestNodeMaterialEditor::deleteNodeUpdatesLinks()
{
    ks::MaterialGraph graph;
    ks::MaterialNode* a = graph.createNode("Math", QPointF(0, 0));
    ks::MaterialNode* b = graph.createNode("Math", QPointF(150, 0));
    ks::MaterialNode* c = graph.createNode("Output", QPointF(300, 0));

    graph.connectNodes(a->id, a->outputs[0].id, b->id, b->inputs[0].id);
    graph.connectNodes(b->id, b->outputs[0].id, c->id, c->inputs[0].id);

    const QString bId = b->id;
    graph.deleteNode(bId);

    QVERIFY(!graph.findNode(bId));
    QVERIFY(!c->inputs[0].isConnected);
    QVERIFY(!a->outputs[0].isConnected);
}

void TestNodeMaterialEditor::texturePathPersists()
{
    ks::MaterialGraph graph;
    ks::MaterialNode* img = graph.createNode("ImageTexture", QPointF(0, 0));
    QVERIFY(img);
    auto* imageNode = dynamic_cast<ks::ImageNode*>(img);
    QVERIFY(imageNode);
    imageNode->texturePath = "C:/maps/diffuse.png";

    const QJsonObject json = graph.toJson();
    QCOMPARE(json["nodes"].toArray().at(0).toObject()["texturePath"].toString(),
             QString("C:/maps/diffuse.png"));

    ks::MaterialGraph restored;
    restored.fromJson(json);
    auto* restoredImg = dynamic_cast<ks::ImageNode*>(restored.nodes.first());
    QVERIFY(restoredImg);
    QCOMPARE(restoredImg->texturePath, QString("C:/maps/diffuse.png"));
}

QTEST_MAIN(TestNodeMaterialEditor)
#include "test_NodeMaterialEditor.moc"
