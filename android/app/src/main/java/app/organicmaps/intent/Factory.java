package app.organicmaps.intent;

import android.content.ContentResolver;
import android.content.Intent;
import android.net.Uri;

import androidx.annotation.NonNull;
import androidx.core.content.IntentCompat;

import app.organicmaps.Framework;
import app.organicmaps.Map;
import app.organicmaps.MwmActivity;
import app.organicmaps.MwmApplication;
import app.organicmaps.api.ParsedRoutingData;
import app.organicmaps.api.ParsedSearchRequest;
import app.organicmaps.api.RequestType;
import app.organicmaps.api.RoutePoint;
import app.organicmaps.bookmarks.data.BookmarkManager;
import app.organicmaps.bookmarks.data.FeatureId;
import app.organicmaps.bookmarks.data.MapObject;
import app.organicmaps.routing.RoutingController;
import app.organicmaps.search.SearchActivity;
import app.organicmaps.search.SearchEngine;
import app.organicmaps.util.StorageUtils;
import app.organicmaps.util.concurrency.ThreadPool;

import java.io.File;

public class Factory
{
  public static boolean isStartedForApiResult(@NonNull Intent intent)
  {
    return (intent.getFlags() & Intent.FLAG_ACTIVITY_FORWARD_RESULT) != 0;
  }

  public static class KmzKmlProcessor implements IntentProcessor
  {
    @Override
    public boolean process(@NonNull Intent intent, @NonNull MwmActivity activity)
    {
      // See KML/KMZ/KMB intent filters in manifest.
      final Uri uri;
      if (Intent.ACTION_VIEW.equals(intent.getAction()))
        uri = intent.getData();
      else if (Intent.ACTION_SEND.equals(intent.getAction()))
        uri = IntentCompat.getParcelableExtra(intent, Intent.EXTRA_STREAM, Uri.class);
      else
        uri = null;
      if (uri == null)
        return false;

      MwmApplication app = MwmApplication.from(activity);
      final File tempDir = new File(StorageUtils.getTempPath(app));
      final ContentResolver resolver = activity.getContentResolver();
      ThreadPool.getStorage().execute(() -> BookmarkManager.INSTANCE.importBookmarksFile(resolver, uri, tempDir));
      return false;
    }
  }

  public static class UrlProcessor implements IntentProcessor
  {
    private static final int SEARCH_IN_VIEWPORT_ZOOM = 16;

    @Override
    public boolean process(@NonNull Intent intent, @NonNull MwmActivity target)
    {
      final Uri uri = intent.getData();
      if (uri == null)
        return false;

      switch (Framework.nativeParseAndSetApiUrl(uri.toString()))
      {
        case RequestType.INCORRECT:
          return false;

        case RequestType.MAP:
          SearchEngine.INSTANCE.cancelInteractiveSearch();
          Map.executeMapApiRequest();
          return true;

        case RequestType.ROUTE:
          SearchEngine.INSTANCE.cancelInteractiveSearch();
          final ParsedRoutingData data = Framework.nativeGetParsedRoutingData();
          RoutingController.get().setRouterType(data.mRouterType);
          final RoutePoint from = data.mPoints[0];
          final RoutePoint to = data.mPoints[1];
          RoutingController.get().prepare(MapObject.createMapObject(FeatureId.EMPTY, MapObject.API_POINT,
                                                                    from.mName, "", from.mLat, from.mLon),
                                          MapObject.createMapObject(FeatureId.EMPTY, MapObject.API_POINT,
                                                                    to.mName, "", to.mLat, to.mLon), true);
          return true;
        case RequestType.SEARCH:
        {
          SearchEngine.INSTANCE.cancelInteractiveSearch();
          final ParsedSearchRequest request = Framework.nativeGetParsedSearchRequest();
          final double[] latlon = Framework.nativeGetParsedCenterLatLon();
          if (latlon != null)
          {
            Framework.nativeStopLocationFollow();
            Framework.nativeSetViewportCenter(latlon[0], latlon[1], SEARCH_IN_VIEWPORT_ZOOM);
            // We need to update viewport for search api manually because of drape engine
            // will not notify subscribers when search activity is shown.
            if (!request.mIsSearchOnMap)
              Framework.nativeSetSearchViewport(latlon[0], latlon[1], SEARCH_IN_VIEWPORT_ZOOM);
          }
          SearchActivity.start(target, request.mQuery, request.mLocale, request.mIsSearchOnMap);
          return true;
        }
        case RequestType.CROSSHAIR:
        {
          SearchEngine.INSTANCE.cancelInteractiveSearch();
          target.showPositionChooserForAPI(Framework.nativeGetParsedAppName());

          final double[] latlon = Framework.nativeGetParsedCenterLatLon();
          if (latlon != null)
          {
            Framework.nativeStopLocationFollow();
            Framework.nativeSetViewportCenter(latlon[0], latlon[1], SEARCH_IN_VIEWPORT_ZOOM);
          }

          return true;
        }
      }

      return false;
    }
  }
}
