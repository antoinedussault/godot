def can_build(env, platform):
    return True

def configure(env):
    pass

def get_doc_classes():
    return [
        "SceneCatalog",
        "SceneCatalogEntry",
        "SceneManager",
        "SceneTransition",
        "SceneSpawnPoint",
        "SceneDoor",
        "WorldLocationArea",
        "CellCharacterBody",
        "CellItemBody",
        "ItemCategory",
        "ItemCategoryCatalog",
    ]

def get_doc_path():
    return "doc_classes"
