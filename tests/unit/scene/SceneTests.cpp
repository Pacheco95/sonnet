#include <catch2/catch_test_macros.hpp>

#include <sonnet/scene/Scene.hpp>

TEST_CASE("Scene_CreateObject_AddsToCollection") {
    sonnet::scene::Scene scene;
    REQUIRE(scene.objects().empty());

    auto& obj = scene.createObject("TestObj");
    REQUIRE(scene.objects().size() == 1);
    REQUIRE(obj.name == "TestObj");
}

TEST_CASE("Scene_CreateMultipleObjects_CountIsCorrect") {
    sonnet::scene::Scene scene;
    scene.createObject("A");
    scene.createObject("B");
    scene.createObject("C");
    REQUIRE(scene.objects().size() == 3);
}

TEST_CASE("Scene_DestroyObject_RemovesFromCollection") {
    sonnet::scene::Scene scene;
    auto& obj = scene.createObject("Target");
    REQUIRE(scene.objects().size() == 1);

    scene.destroyObject(&obj);
    REQUIRE(scene.objects().empty());
}

TEST_CASE("Scene_CreateObject_WithParent_SetsHierarchy") {
    sonnet::scene::Scene scene;
    auto& parent = scene.createObject("Parent");
    auto& child  = scene.createObject("Child", &parent);

    REQUIRE(child.transform.getParent() == &parent.transform);
}

TEST_CASE("Scene_CircularParentAttempt_HierarchyUnchanged") {
    sonnet::scene::Scene scene;
    auto& parent = scene.createObject("Parent");
    auto& child  = scene.createObject("Child", &parent);

    // Attempting to make parent a child of child would be circular — must be rejected.
    parent.transform.setParent(&child.transform, false);

    REQUIRE(parent.transform.getParent() == nullptr);
    REQUIRE(child.transform.getParent() == &parent.transform);
}
