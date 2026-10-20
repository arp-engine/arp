#include "2d/scene_2d_render_queue.h"

#include <utility>

namespace arp::scene2d {

void Scene2DRenderQueue::addTileMapChunk(Scene2DRenderItem item)
{
    ++tileMapItemCount_;
    items_.push_back(std::move(item));
}

void Scene2DRenderQueue::addSprite(Scene2DRenderItem item)
{
    ++spriteItemCount_;
    items_.push_back(std::move(item));
}

} // namespace arp::scene2d
