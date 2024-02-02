package app.organicmaps.car.screens;

import androidx.annotation.NonNull;
import androidx.car.app.CarContext;
import androidx.car.app.model.Action;
import androidx.car.app.model.CarIcon;
import androidx.car.app.model.Header;
import androidx.car.app.model.Item;
import androidx.car.app.model.ItemList;
import androidx.car.app.model.Row;
import androidx.car.app.model.Template;
import androidx.car.app.navigation.model.MapTemplate;
import androidx.core.graphics.drawable.IconCompat;

import app.organicmaps.R;
import app.organicmaps.car.SurfaceRenderer;
import app.organicmaps.car.screens.base.BaseMapScreen;
import app.organicmaps.car.screens.search.SearchScreen;
import app.organicmaps.car.util.SuggestionsHelpers;
import app.organicmaps.car.util.UiHelpers;

public class MapScreen extends BaseMapScreen
{
  public MapScreen(@NonNull CarContext carContext, @NonNull SurfaceRenderer surfaceRenderer)
  {
    super(carContext, surfaceRenderer);
  }

  @NonNull
  @Override
  public Template onGetTemplate()
  {
    SuggestionsHelpers.updateSuggestions(getCarContext());

    final MapTemplate.Builder builder = new MapTemplate.Builder();
    builder.setHeader(createHeader());
    builder.setMapController(UiHelpers.createMapController(getCarContext(), getSurfaceRenderer()));
    builder.setActionStrip(UiHelpers.createSettingsActionStrip(this, getSurfaceRenderer()));
    builder.setItemList(createList());
    return builder.build();
  }

  @NonNull
  private Header createHeader()
  {
    final Header.Builder builder = new Header.Builder();
    builder.setStartHeaderAction(new Action.Builder(Action.APP_ICON).build());
    builder.setTitle(getCarContext().getString(R.string.app_name));
    return builder.build();
  }

  @NonNull
  private ItemList createList()
  {
    final ItemList.Builder builder = new ItemList.Builder();
    builder.addItem(createSearchItem());
    builder.addItem(createCategoriesItem());
    builder.addItem(createBookmarksItem());
    return builder.build();
  }

  @NonNull
  private Item createSearchItem()
  {
    final CarIcon iconSearch = new CarIcon.Builder(IconCompat.createWithResource(getCarContext(), R.drawable.ic_search)).build();

    final Row.Builder builder = new Row.Builder();
    builder.setTitle(getCarContext().getString(R.string.search));
    builder.setImage(iconSearch);
    builder.setBrowsable(true);
    builder.setOnClickListener(this::openSearch);
    return builder.build();
  }

  @NonNull
  private Item createCategoriesItem()
  {
    final Row.Builder builder = new Row.Builder();
    builder.setTitle(getCarContext().getString(R.string.categories));
    builder.setBrowsable(true);
    builder.setOnClickListener(this::openCategories);
    return builder.build();
  }

  @NonNull
  private Item createBookmarksItem()
  {
    final Row.Builder builder = new Row.Builder();
    builder.setTitle(getCarContext().getString(R.string.bookmarks));
    builder.setBrowsable(true);
    builder.setOnClickListener(this::openBookmarks);
    return builder.build();
  }

  private void openSearch()
  {
    // Details in UiHelpers.createSettingsAction()
    if (getScreenManager().getTop() != this)
      return;
    getScreenManager().push(new SearchScreen.Builder(getCarContext(), getSurfaceRenderer()).build());
  }

  private void openCategories()
  {
    // Details in UiHelpers.createSettingsAction()
    if (getScreenManager().getTop() != this)
      return;
    getScreenManager().push(new CategoriesScreen(getCarContext(), getSurfaceRenderer()));
  }

  private void openBookmarks()
  {
    // Details in UiHelpers.createSettingsAction()
    if (getScreenManager().getTop() != this)
      return;
    getScreenManager().push(new BookmarksScreen(getCarContext(), getSurfaceRenderer()));
  }
}
