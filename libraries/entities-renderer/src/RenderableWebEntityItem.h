//
//  Created by Bradley Austin Davis on 2015/05/12
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_RenderableWebEntityItem_h
#define hifi_RenderableWebEntityItem_h

#include <WebEntityItem.h>
#include "RenderableEntityItem.h"

class OffscreenQmlSurface;
class PointerEvent;

namespace render { namespace entities {

class WebEntityRenderer : public TypedEntityRenderer<WebEntityItem> {
    Q_OBJECT
    using Parent = TypedEntityRenderer<WebEntityItem>;
    friend class EntityRenderer;

public:
    WebEntityRenderer(const EntityItemPointer& entity);

    Q_INVOKABLE void hoverEnterEntity(const PointerEvent& event);
    Q_INVOKABLE void hoverLeaveEntity(const PointerEvent& event);
    Q_INVOKABLE void handlePointerEvent(const PointerEvent& event);

protected:
    virtual void onRemoveFromSceneTyped(const TypedEntityPointer& entity) override;
    virtual bool needsRenderUpdate() const override;
    virtual bool needsRenderUpdateFromTypedEntity(const TypedEntityPointer& entity) const override;
    virtual void doRenderUpdateSynchronousTyped(const ScenePointer& scene, Transaction& transaction, const TypedEntityPointer& entity) override;
    virtual void doRender(RenderArgs* args) override;
    virtual bool isTransparent() const override;

    virtual bool wantsHandControllerPointerEvents() const override { return true; }
    virtual bool wantsKeyboardFocus() const override { return true; }
    virtual void setProxyWindow(QWindow* proxyWindow) override;
    virtual QObject* getEventHandler() override;

private:
    void onTimeout();
    bool buildWebSurface(const TypedEntityPointer& entity);
    void destroyWebSurface();
    bool hasWebSurface();
    glm::vec2 getWindowSize(const TypedEntityPointer& entity) const;


    int _geometryId{ 0 };
    enum class ContentType {
        NoContent,
        HtmlContent,
        QmlContent
    };

    static ContentType getContentType(const QString& urlString);

    ContentType _contentType{ ContentType::NoContent };
    QSharedPointer<OffscreenQmlSurface> _webSurface;
    glm::vec3 _contextPosition;
    gpu::TexturePointer _texture;
    QString _lastSourceUrl;
    uint16_t _lastDPI;
    bool _lastLocked;
    QTimer _timer;
    uint64_t _lastRenderTime { 0 };
    Transform _renderTransform;
};

} } // namespace 

#if 0
    virtual void emitScriptEvent(const QVariant& message) override;
#endif

#endif // hifi_RenderableWebEntityItem_h
