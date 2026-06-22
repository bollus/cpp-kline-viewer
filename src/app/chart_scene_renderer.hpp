#pragma once

#include <QColor>
#include <QLineF>
#include <QRectF>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QVector>

namespace q4j::chart_scene {

inline QSGGeometryNode *createRectNode(const QRectF &rect, const QColor &color) {
  auto *node = new QSGGeometryNode;
  auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 6);
  geometry->setDrawingMode(QSGGeometry::DrawTriangles);
  auto *vertices = geometry->vertexDataAsPoint2D();
  vertices[0].set(rect.left(), rect.top());
  vertices[1].set(rect.right(), rect.top());
  vertices[2].set(rect.left(), rect.bottom());
  vertices[3].set(rect.left(), rect.bottom());
  vertices[4].set(rect.right(), rect.top());
  vertices[5].set(rect.right(), rect.bottom());

  auto *material = new QSGFlatColorMaterial;
  material->setColor(color);
  node->setGeometry(geometry);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(material);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

inline QSGGeometryNode *createRectBatchNode(const QVector<QRectF> &rects, const QColor &color) {
  if (rects.isEmpty()) return nullptr;
  auto *node = new QSGGeometryNode;
  auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), rects.size() * 6);
  geometry->setDrawingMode(QSGGeometry::DrawTriangles);
  auto *vertices = geometry->vertexDataAsPoint2D();
  int offset = 0;
  for (const QRectF &rawRect : rects) {
    const QRectF rect = rawRect.normalized();
    vertices[offset++].set(rect.left(), rect.top());
    vertices[offset++].set(rect.right(), rect.top());
    vertices[offset++].set(rect.left(), rect.bottom());
    vertices[offset++].set(rect.left(), rect.bottom());
    vertices[offset++].set(rect.right(), rect.top());
    vertices[offset++].set(rect.right(), rect.bottom());
  }

  auto *material = new QSGFlatColorMaterial;
  material->setColor(color);
  node->setGeometry(geometry);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(material);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

inline QSGGeometryNode *createLineBatchNode(const QVector<QLineF> &lines, const QColor &color) {
  if (lines.isEmpty()) return nullptr;
  auto *node = new QSGGeometryNode;
  auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), lines.size() * 2);
  geometry->setDrawingMode(QSGGeometry::DrawLines);
  auto *vertices = geometry->vertexDataAsPoint2D();
  int offset = 0;
  for (const QLineF &line : lines) {
    vertices[offset++].set(line.x1(), line.y1());
    vertices[offset++].set(line.x2(), line.y2());
  }

  auto *material = new QSGFlatColorMaterial;
  material->setColor(color);
  node->setGeometry(geometry);
  node->setFlag(QSGNode::OwnsGeometry);
  node->setMaterial(material);
  node->setFlag(QSGNode::OwnsMaterial);
  return node;
}

}
